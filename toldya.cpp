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
  std::mutex potential_mutex;
  
  twitter::client client(auth);
  std::set<twitter::user_id> streamed_friends;
  client.setUserStreamNotifyCallback([&] (twitter::notification n) {
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
        std::cout << n.getTweet().getText() << std::endl;
        
        potential.push_back(n.getTweet());
      }
    } else if (n.getType() == twitter::notification::type::followed)
    {
      twitter::response resp = client.follow(n.getUser());
      if (resp != twitter::response::ok)
      {
        std::cout << "Twitter error while following @" << n.getUser().getScreenName() << ": " << resp << std::endl;
      }
    }
  });
  
  std::this_thread::sleep_for(std::chrono::minutes(1));
  
  std::cout << "Starting streaming" << std::endl;
  client.startUserStream();
  
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
    std::set<twitter::user_id> friends;
    std::set<twitter::user_id> followers;
    twitter::response resp = client.getFriends(friends);
    if (resp == twitter::response::ok)
    {
      resp = client.getFollowers(followers);
      if (resp == twitter::response::ok)
      {
        std::list<twitter::user_id> old_friends, new_followers;
        std::set_difference(std::begin(friends), std::end(friends), std::begin(followers), std::end(followers), std::back_inserter(old_friends));
        std::set_difference(std::begin(followers), std::end(followers), std::begin(friends), std::end(friends), std::back_inserter(new_followers));
        
        std::set<twitter::user_id> old_friends_set;
        for (auto f : old_friends)
        {
          old_friends_set.insert(f);
          
          resp = client.unfollow(f);
          if (resp != twitter::response::ok)
          {
            std::cout << "Twitter error while unfollowing" << std::endl;
          }
        }
        
        for (auto f : new_followers)
        {
          resp = client.follow(f);
          if (resp != twitter::response::ok)
          {
            std::cout << "Twitter error while following" << std::endl;
          }
        }
        
        std::lock_guard<std::mutex> potential_guard(potential_mutex);
        std::vector<twitter::tweet> to_keep;
        for (auto pt : potential)
        {
          if (old_friends_set.count(pt.getAuthor()) == 0)
          {
            to_keep.push_back(pt);
          }
        }
        
        potential = to_keep;
      } else {
        std::cout << "Twitter error while getting followers: " << resp << std::endl;
      }
    } else {
      std::cout << "Twitter error while getting friends: " << resp << std::endl;
    }
    
    // Tweet!
    if (!potential.empty())
    {
      auto to_quote = potential[rand() % potential.size()];
      potential.clear();
      
      std::string caption = captions[rand() % captions.size()];
      std::string doc = caption + " " + to_quote.getURL();
      
      twitter::tweet sent;
      twitter::response resp = client.updateStatus(doc, sent);
      if (resp != twitter::response::ok)
      {
        std::cout << "Error tweeting: " << resp << std::endl;
      }
    }
  }
  
  client.stopUserStream();
}
