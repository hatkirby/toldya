#include <twitter.h>
#include <yaml-cpp/yaml.h>
#include <mutex>
#include <thread>
#include <ctime>
#include <chrono>
#include <iostream>
#include <algorithm>
#include <random>

int main(int argc, char** argv)
{
  if (argc != 2)
  {
    std::cout << "usage: toldya [configfile]" << std::endl;
    return -1;
  }

  std::random_device randomDevice;
  std::mt19937 rng(randomDevice());

  std::string configfile(argv[1]);
  YAML::Node config = YAML::LoadFile(configfile);

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

  std::map<twitter::user_id, std::vector<twitter::tweet>> potential;
  std::set<twitter::tweet_id> deletions;
  std::mutex potentialMutex;

  twitter::client client(auth);
  std::set<twitter::user_id> streamedFriends;

  std::cout << "Starting streaming" << std::endl;

  twitter::stream userStream(client, [&] (twitter::notification n) {
    if (n.getType() == twitter::notification::type::friends)
    {
      streamedFriends = n.getFriends();
    } else if (n.getType() == twitter::notification::type::follow)
    {
      streamedFriends.insert(n.getUser().getID());
    } else if (n.getType() == twitter::notification::type::unfollow)
    {
      streamedFriends.erase(n.getUser().getID());
    } else if (n.getType() == twitter::notification::type::tweet)
    {
      // Only monitor people you are following
      // Ignore retweets
      // Ignore messages
      if (
        (streamedFriends.count(n.getTweet().getAuthor().getID()) == 1)
        && (!n.getTweet().isRetweet())
        && (n.getTweet().getText().front() != '@')
      )
      {
        std::lock_guard<std::mutex> potentialGuard(potentialMutex);
        std::cout << n.getTweet().getID() << ": " << n.getTweet().getText()
          << std::endl;

        potential[n.getTweet().getAuthor().getID()].
          push_back(std::move(n.getTweet()));
      }
    } else if (n.getType() == twitter::notification::type::followed)
    {
      try
      {
        client.follow(n.getUser());
      } catch (const twitter::twitter_error& error)
      {
        std::cout << "Twitter error while following @"
          << n.getUser().getScreenName() << ": " << error.what() << std::endl;
      }
    } else if (n.getType() == twitter::notification::type::deletion)
    {
      std::lock_guard<std::mutex> potentialGuard(potentialMutex);
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
    auto to_wait = std::chrono::duration_cast<std::chrono::seconds>(
      (to_until + std::chrono::hours(24 + 9))
        - std::chrono::system_clock::now());

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
      std::cout << "Sleeping for " << (waitlen/60) << " minutes..."
        << std::endl;
    } else if (waitlen == 60*60)
    {
      std::cout << "Sleeping for 1 hour..." << std::endl;
    } else if (waitlen < 60*60*24)
    {
      std::cout << "Sleeping for " << (waitlen/60/60) << " hours..."
        << std::endl;
    } else if (waitlen == 60*60*24)
    {
      std::cout << "Sleeping for 1 day..." << std::endl;
    } else {
      std::cout << "Sleeping for " << (waitlen/60/60/24) << " days..."
        << std::endl;
    }

    std::this_thread::sleep_for(to_wait);

    // The rest of the loop deals with the potential tweets
    std::lock_guard<std::mutex> potentialGuard(potentialMutex);

    // Unfollow people who have unfollowed us
    try
    {
      std::set<twitter::user_id> friends = client.getFriends();
      std::set<twitter::user_id> followers = client.getFollowers();

      std::list<twitter::user_id> oldFriends;
      std::set_difference(
        std::begin(friends),
        std::end(friends),
        std::begin(followers),
        std::end(followers),
        std::back_inserter(oldFriends));

      std::list<twitter::user_id> newFollowers;
      std::set_difference(
        std::begin(followers),
        std::end(followers),
        std::begin(friends),
        std::end(friends),
        std::back_inserter(newFollowers));

      std::set<twitter::user_id> oldFriendsSet;
      for (twitter::user_id f : oldFriends)
      {
        oldFriendsSet.insert(f);

        try
        {
          client.unfollow(f);
        } catch (const twitter::twitter_error& error)
        {
          std::cout << "Twitter error while unfollowing: " << error.what()
            << std::endl;
        }
      }

      for (twitter::user_id f : newFollowers)
      {
        try
        {
          client.follow(f);
        } catch (const twitter::twitter_error& error)
        {
          std::cout << "Twitter error while following: " << error.what()
            << std::endl;
        }
      }

      // Filter the potential tweets for users that are still following us, and
      // and for tweets that haven't been deleted.
      std::map<twitter::user_id, std::vector<twitter::tweet>> toKeep;

      for (auto& p : potential)
      {
        // The author has not unfollowed
        if (!oldFriendsSet.count(p.first))
        {
          std::vector<twitter::tweet> userTweets;

          for (twitter::tweet& pt : p.second)
          {
            // The tweet was not deleted
            if (!deletions.count(pt.getID()))
            {
              userTweets.push_back(std::move(pt));
            }
          }

          if (!userTweets.empty())
          {
            toKeep[p.first] = std::move(userTweets);
          }
        }
      }

      potential = std::move(toKeep);
      deletions.clear();
    } catch (const twitter::twitter_error& error)
    {
      std::cout << "Twitter error while getting friends/followers: "
        << error.what() << std::endl;
    }

    // Tweet!
    if (!potential.empty())
    {
      std::uniform_int_distribution<size_t> userDist(0, potential.size() - 1);
      const std::vector<twitter::tweet>& toQuoteUser =
        std::next(std::begin(potential), userDist(rng))->second;

      std::uniform_int_distribution<size_t> postDist(0, toQuoteUser.size() - 1);
      const twitter::tweet& toQuote = toQuoteUser.at(postDist(rng));

      std::uniform_int_distribution<size_t> captionDist(0, captions.size() - 1);
      const std::string& caption = captions.at(captionDist(rng));

      std::string doc = caption + " " + toQuote.getURL();

      try
      {
        client.updateStatus(doc);

        std::cout << "Tweeted!" << std::endl;
      } catch (const twitter::twitter_error& error)
      {
        std::cout << "Error tweeting: " << error.what() << std::endl;
      }

      potential.clear();
    }
  }
}
