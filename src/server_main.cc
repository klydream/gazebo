/*
 * Copyright 2011 Nate Koenig & Andrew Howard
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
*/
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <iostream>
#include <boost/interprocess/sync/interprocess_semaphore.hpp>

#include "gazebo_config.h"
#include "common/CommonTypes.hh"
#include "rendering/Rendering.hh"
#include "common/SystemPaths.hh"
#include "Server.hh"

gazebo::Server *server = NULL;

std::string config_filename = "";
gazebo::common::StrStr_M params;
std::vector<std::string> plugins;

boost::interprocess::interprocess_semaphore sem(0);

//////////////////////////////////////////////////
void PrintUsage()
{
  std::cerr << "Usage: gzserver\n";
}

//////////////////////////////////////////////////
void PrintVersion()
{
  fprintf(stderr, "%s", GAZEBO_VERSION_HEADER);
}

//////////////////////////////////////////////////
int ParseArgs(int argc, char **argv)
{
  // FILE *tmpFile;
  int ch;

  char *flags = const_cast<char*>("up:");
  // Get letter options
  while ((ch = getopt(argc, argv, flags)) != -1)
  {
    switch (ch)
    {
      case 'p':
        {
          if (optarg != NULL)
            plugins.push_back(std::string(optarg));
          else
            gzerr << "Missing plugin filename with -p argument\n";
          break;
        }

      case 'u':
        params["pause"] = "true";
        break;
      default:
        PrintUsage();
        return -1;
    }
  }

  argc -= optind;
  argv += optind;

  // Get the world file name
  if (argc >= 1)
    config_filename = argv[0];

  return 0;
}


//////////////////////////////////////////////////
void SignalHandler(int)
{
  server->Stop();
}

int main(int argc, char **argv)
{
  // Application Setup
  if (ParseArgs(argc, argv) != 0)
    return -1;

  PrintVersion();

  if (signal(SIGINT, SignalHandler) == SIG_ERR)
  {
    std::cerr << "signal(2) failed while setting up for SIGINT" << std::endl;
    return -1;
  }

  server = new gazebo::Server();
  if (config_filename.empty())
  {
    printf("Warning: no world filename specified, using default world\n");
    config_filename = "worlds/empty.world";
  }

  // Construct plugins
  /// Load all the plugins specified on the command line
  for (std::vector<std::string>::iterator iter = plugins.begin();
       iter != plugins.end(); ++iter)
  {
    server->LoadPlugin(*iter);
  }

  if (!server->Load(config_filename))
  {
    gzerr << "Could not open file[" << config_filename << "]\n";
    return -1;
  }

  server->SetParams(params);
  server->Init();

  server->Run();

  server->Fini();

  delete server;
  server = NULL;

  printf("\n");
  return 0;
}


