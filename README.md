
The Network Simulator, Version 3
================================

## Table of Contents:

1) [An overview](#an-open-source-project)
2) [Building ns-3](#building-ns-3)
3) [Running ns-3](#running-ns-3)
4) [Files modified or developed for Bolt](#files-modified-or-developed-for-bolt)
5) [Getting access to the ns-3 documentation](#getting-access-to-the-ns-3-documentation)
6) [Working with the development version of ns-3](#working-with-the-development-version-of-ns-3)

Note:  Much more substantial information about ns-3 can be found at
https://www.nsnam.org

## An Open Source project

ns-3 is a free open source project aiming to build a discrete-event
network simulator targeted for simulation research and education.
This is a collaborative project; we hope that
the missing pieces of the models we have not yet implemented
will be contributed by the community in an open collaboration
process.

The process of contributing to the ns-3 project varies with
the people involved, the amount of time they can invest
and the type of model they want to work on, but the current
process that the project tries to follow is described here:
https://www.nsnam.org/developers/contributing-code/

This README excerpts some details from a more extensive
tutorial that is maintained at:
https://www.nsnam.org/documentation/latest/

## Building ns-3

The code for the framework and the default models provided
by ns-3 is built as a set of libraries. User simulations
are expected to be written as simple programs that make
use of these ns-3 libraries.

To build the set of default libraries and the example
programs included in this package, you need to use the
tool 'waf'. Detailed information on how to use waf is
included in the file doc/build.txt

However, the real quick and dirty way to get started is to
type the command
```shell
./waf configure
```

followed by

```shell
./waf
```

in the directory which contains this README file. The files
built will be copied in the build/ directory.

The current codebase is expected to build and run on the
set of platforms listed in the [release notes](RELEASE_NOTES)
file.

Other platforms may or may not work: we welcome patches to
improve the portability of the code to these other platforms.

## Running ns-3

The scripts for different simulation scerios of Bolt are listed 
under `scratch/` directory.

For example, the simulations with the dumbbell topology can be run with:
```shell
$./waf --run "scratch/bolt-link-utilization-experiment --traceQueues --duration=0.1
```

Please note that each script has a list of input arguments that can be 
played with to run Bolt or Swift with different configurations. See the 
script files for the list of accepted arguments.

## Files modified or developed for Bolt

The main files edited or developed for the implementation of Bolt are:

`src/internet/model/bolt-header.cc/h`: Declaration of the Bolt packet header

`src/internet/model/bolt-l4-protocol.cc/h`: Main algorithm logic for end-hosts

`src/traffic-control/model/pfifo-bolt-queue-disc.cc`: Switch logic of Bolt

`src/applications/model/msg-generator-app.cc/h`: Traffic generator with given flow size distributions

`src/internet/model/ipv4-global-routing.cc/h`: adds ECMP feature

Note that there may be other modified files for smal functionality that is not 
fundamental to how Bolt works. Therefore they are not listed above.

## Getting access to the ns-3 documentation

Once you have verified that your build of ns-3 works by running
the simple-point-to-point example as outlined in 3) above, it is
quite likely that you will want to get started on reading
some ns-3 documentation.

All of that documentation should always be available from
the ns-3 website: https://www.nsnam.org/documentation/.

This documentation includes:

  - a tutorial

  - a reference manual

  - models in the ns-3 model library

  - a wiki for user-contributed tips: https://www.nsnam.org/wiki/

  - API documentation generated using doxygen: this is
    a reference manual, most likely not very well suited
    as introductory text:
    https://www.nsnam.org/doxygen/index.html

## Working with the development version of ns-3

If you want to download and use the development version of ns-3, you
need to use the tool `git`. A quick and dirty cheat sheet is included
in the manual, but reading through the git
tutorials found in the Internet is usually a good idea if you are not
familiar with it.

If you have successfully installed git, you can get
a copy of the development version with the following command:
```shell
git clone https://github.com/serhatarslan-hub/bolt-cc-ns3.git
```

However, we recommend to follow the Gitlab guidelines for starters,
that includes creating a Gitlab account, forking the ns-3-dev project
under the new account's name, and then cloning the forked repository.
You can find more information in the [manual](https://www.nsnam.org/docs/manual/html/working-with-git.html).
