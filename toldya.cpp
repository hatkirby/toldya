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

  twitter::auth auth(
    config["consumer_key"].as<std::string>(),
    config["consumer_secret"].as<std::string>(),
    config["access_key"].as<std::string>(),
    config["access_secret"].as<std::string>());

  std::vector<std::string> captions {
    "It begins.",
    "Yikes.",
    "Frightening.",
    "This is how it starts..."
  };

  std::map<twitter::user_id, std::vector<twitter::tweet>> potential;
  std::set<twitter::tweet_id> tweetIds;

  twitter::client client(auth);
  std::set<twitter::user_id> friends = client.getFriends();

  for (;;)
  {
    // Poll every 5 minutes
    auto midtime = time(NULL);
    auto midtm = localtime(&midtime);

    // At 9am, do the daily tweet
    bool shouldTweet = false;
    if (midtm->tm_hour == 8 && midtm->tm_min >= 55)
    {
      shouldTweet = true;

      std::cout << "Sleeping for 5 minutes (will tweet)..." << std::endl;
    } else {
      std::cout << "Sleeping for 5 minutes..." << std::endl;
    }

    std::this_thread::sleep_for(std::chrono::minutes(5));

    std::list<twitter::tweet> newTweets = client.getHomeTimeline().poll();
    for (twitter::tweet& nt : newTweets)
    {
      // Only monitor people you are following
      // Ignore retweets
      // Ignore messages
      if (
        (friends.count(nt.getAuthor().getID()) == 1)
        && (!nt.isRetweet())
        && (nt.getText().front() != '@')
      )
      {
        std::cout << nt.getID() << ": " << nt.getText() << std::endl;

        tweetIds.insert(nt.getID());
        potential[nt.getAuthor().getID()].emplace_back(std::move(nt));
      }
    }

    newTweets.clear();

    // The rest of the loop is once-a-day
    if (!shouldTweet)
    {
      continue;
    }

    // Unfollow people who have unfollowed us
    try
    {
      friends = client.getFriends();

      std::set<twitter::user_id> followers = client.getFollowers();

      std::list<twitter::user_id> oldFriends;
      std::set_difference(
        std::begin(friends),
        std::end(friends),
        std::begin(followers),
        std::end(followers),
        std::back_inserter(oldFriends));

      std::set<twitter::user_id> newFollowers;
      std::set_difference(
        std::begin(followers),
        std::end(followers),
        std::begin(friends),
        std::end(friends),
        std::inserter(newFollowers, std::begin(newFollowers)));

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

      std::list<twitter::user> newFollowerObjs =
        client.hydrateUsers(std::move(newFollowers));

      for (const twitter::user& f : newFollowerObjs)
      {
        if (!f.isProtected())
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
      }

      // Hydrate the tweets we've received to make sure that none of them have
      // been deleted.
      std::list<twitter::tweet> hydrated = client.hydrateTweets(tweetIds);
      tweetIds.clear();

      std::set<twitter::tweet_id> hydratedIds;
      for (twitter::tweet& tw : hydrated)
      {
        hydratedIds.insert(tw.getID());
      }

      hydrated.clear();

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
            if (hydratedIds.count(pt.getID()))
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
