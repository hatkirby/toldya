#include <twitter.h>
#include <yaml-cpp/yaml.h>
#include <mutex>
#include <thread>
#include <ctime>
#include <chrono>
#include <iostream>
#include <algorithm>

int main(int argc, char** argv)
{
  YAML::Node config = YAML::LoadFile("config.yml");
    
  twitter::auth auth;
  auth.setConsumerKey(config["consumer_key"].as<std::string>());
  auth.setConsumerSecret(config["consumer_secret"].as<std::string>());
  auth.setAccessKey(config["access_key"].as<std::string>());
  auth.setAccessSecret(config["access_secret"].as<std::string>());
  
  std::vector<std::string> captions {
    "It begins.",
    "Yikes.",
    "Frightening.",
    "This is how it starts..."
  };
  
  std::vector<twitter::tweet> potential;
  std::set<twitter::tweet_id> deletions;
  std::mutex potential_mutex;
  
  twitter::client client(auth);
  std::set<twitter::user_id> streamed_friends;
  
  std::cout << "Starting streaming" << std::endl;
  
  twitter::stream user_stream(client, [&] (twitter::notification n) {
    if (n.getType() == twitter::notification::type::friends)
    {
      streamed_friends = n.getFriends();
    } else if (n.getType() == twitter::notification::type::follow)
    {
      streamed_friends.insert(n.getUser().getID());
    } else if (n.getType() == twitter::notification::type::unfollow)
    {
      streamed_friends.erase(n.getUser().getID());
    } else if (n.getType() == twitter::notification::type::tweet)
    {
      if (
        (streamed_friends.count(n.getTweet().getAuthor().getID()) == 1) // Only monitor people you are following
        && (!n.getTweet().isRetweet()) // Ignore retweets
        && (n.getTweet().getText().front() != '@') // Ignore messages
      )
      {
        std::lock_guard<std::mutex> potential_guard(potential_mutex);
        std::cout << n.getTweet().getID() << ": " << n.getTweet().getText() << std::endl;
        
        potential.push_back(std::move(n.getTweet()));
      }
    } else if (n.getType() == twitter::notification::type::followed)
    {
      try
      {
        client.follow(n.getUser());
      } catch (const twitter::twitter_error& error)
      {
        std::cout << "Twitter error while following @" << n.getUser().getScreenName() << ": " << error.what() << std::endl;
      }
    } else if (n.getType() == twitter::notification::type::deletion)
    {
      std::lock_guard<std::mutex> potential_guard(potential_mutex);
      std::cout << "Tweet " << n.getTweetID() << " was deleted." << std::endl;
      
      deletions.insert(n.getTweetID());
    }
  });
  
  std::this_thread::sleep_for(std::chrono::minutes(1));
  
  for (;;)
  {
    // Wait until 9am
    auto midtime = time(NULL);
    auto midtm = localtime(&midtime);
    midtm->tm_hour = 0;
    midtm->tm_min = 0;
    midtm->tm_sec = 0;
    auto to_until = std::chrono::system_clock::from_time_t(std::mktime(midtm));
    auto to_wait = std::chrono::duration_cast<std::chrono::seconds>((to_until + std::chrono::hours(24 + 9)) - std::chrono::system_clock::now());
    int waitlen = to_wait.count();
    if (waitlen == 0)
    {
      continue;
    } else if (waitlen == 1)
    {
      std::cout << "Sleeping for 1 second..." << std::endl;
    } else if (waitlen < 60)
    {
      std::cout << "Sleeping for " << waitlen << " seconds..." << std::endl;
    } else if (waitlen == 60)
    {
      std::cout << "Sleeping for 1 minute..." << std::endl;
    } else if (waitlen < 60*60)
    {
      std::cout << "Sleeping for " << (waitlen/60) << " minutes..." << std::endl;
    } else if (waitlen == 60*60)
    {
      std::cout << "Sleeping for 1 hour..." << std::endl;
    } else if (waitlen < 60*60*24)
    {
      std::cout << "Sleeping for " << (waitlen/60/60) << " hours..." << std::endl;
    } else if (waitlen == 60*60*24)
    {
      std::cout << "Sleeping for 1 day..." << std::endl;
    } else {
      std::cout << "Sleeping for " << (waitlen/60/60/24) << " days..." << std::endl;
    }
    
    std::this_thread::sleep_for(to_wait);
    
    // Unfollow people who have unfollowed us
    try
    {
      std::set<twitter::user_id> friends = client.getFriends();
      std::set<twitter::user_id> followers = client.getFollowers();

      std::list<twitter::user_id> old_friends;
      std::list<twitter::user_id> new_followers;
      std::set_difference(std::begin(friends), std::end(friends), std::begin(followers), std::end(followers), std::back_inserter(old_friends));
      std::set_difference(std::begin(followers), std::end(followers), std::begin(friends), std::end(friends), std::back_inserter(new_followers));
      
      std::set<twitter::user_id> old_friends_set;
      for (auto f : old_friends)
      {
        old_friends_set.insert(f);
        
        try
        {
          client.unfollow(f);
        } catch (const twitter::twitter_error& error)
        {
          std::cout << "Twitter error while unfollowing: " << error.what() << std::endl;
        }
      }
      
      for (auto f : new_followers)
      {
        try
        {
          client.follow(f);
        } catch (const twitter::twitter_error& error)
        {
          std::cout << "Twitter error while following: " << error.what() << std::endl;
        }
      }
      
      std::lock_guard<std::mutex> potential_guard(potential_mutex);
      std::vector<twitter::tweet> to_keep;
      for (auto& pt : potential)
      {
        if (
          (old_friends_set.count(pt.getAuthor().getID()) == 0) && // The author has not unfollowed
          (deletions.count(pt.getID()) == 0)) // The tweet was not deleted
        {
          to_keep.push_back(std::move(pt));
        }
      }
      
      potential = std::move(to_keep);
      deletions.clear();
    } catch (const twitter::twitter_error& error)
    {
      std::cout << "Twitter error while getting friends/followers: " << error.what() << std::endl;
    }
    
    // Tweet!
    if (!potential.empty())
    {
      auto to_quote = std::move(potential[rand() % potential.size()]);
      potential.clear();
      
      std::string caption = captions[rand() % captions.size()];
      std::string doc = caption + " " + to_quote.getURL();
      
      try
      {
        client.updateStatus(doc);
        
        std::cout << "Tweeted!" << std::endl;
      } catch (const twitter::twitter_error& error)
      {
        std::cout << "Error tweeting: " << error.what() << std::endl;
      }
    }
  }
}
