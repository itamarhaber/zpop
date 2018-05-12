# Ze POP Redis Module

An educational [Redis](https://redis.io) module that implements ze [top requested](https://github.com/antirez/redis/issues/1861) Redis feature by users.

Has hopes (or the vanity) of becoming a part of ze core one day - see pull request [_"#4879 Implements [B]Z[REV]POP and the respective unit tests"_](https://github.com/antirez/redis/pull/4879) /cc @antirez ;)

Developed w/ <3 for RedisConf 18 and ze Redis community.

## Current status

Fresh out of development, and needs tests -> proof of concept.

**WARNING**: TOTALLY UNSAFE FOR USE WITH ANY SYSTEM THAT USES `MULTI/EXEC` AND/OR LUA SCRIPTS

## Quickstart (or Ze Demo)

### Terminal window #1

Follow the instructions below on how to [build and run Ze POP](#Building and running the module), or just use docker ;)

```text
$ docker run -it -p 6379:6379 --name ZePOP itamarhaber/zpop
1:C 02 May 14:36:18.660 # oO0OoO0OoO0Oo Redis is starting oO0OoO0OoO0Oo
...
1:M 02 May 14:36:18.661 * Module 'ZePOP' loaded from /usr/lib/redis/modules/zpop.so
1:M 02 May 14:36:18.661 * Ready to accept connections
```

### Terminal window #2

```text
$ redis-cli
127.0.0.1:6379> ZADD z 0 a 1 b
(integer) 2
127.0.0.1:6379> Z.POP z
1) "0.000000"
2) "a"
127.0.0.1:6379> Z.POP z
1) "1.000000"
2) "b"
127.0.0.1:6379> Z.POP z
(nil)
127.0.0.1:6379> ZADD z 0 a 1 b
(integer) 2
127.0.0.1:6379> 3 Z.REVPOP z
1) "1.000000"
2) "b"
1) "0.000000"
2) "a"
(nil)
127.0.0.1:6379> Z.BPOP z1 z2 z3 0
| <- imagine zat zis iz an indefinitely blinking cursor
```

### Terminal window #3
```text
$ redis-cli
127.0.0.1:6379> ZADD z2 6379 foobar
(integer) 1
```

### And back to terminal window #2

```text
1) "z2"
2) "6379.000000"
3) "foobar"
127.0.0.1:6379> |
```

## Educational?!? Use your imagination...

Yeah. It is originally intended as teaching/guiding aid for the newly-initiated Redis Module developer as part of the day 0 training at RedisConf 18. I felt I had to refresh upon the stale-ish 2yo HGETSET example, so that's basically ZPOP using the High Level Redis Module API.

But you don't use the HL API unless you know better, and I guess I know a little better, so there's the (better!) LL implementation that's actually being used by the module more "advanced" stuff.

Oh, it also shows how to inspect the command's name in order to implement both ZPOP and ZREVPOP efficiently.

Then, because blocking operations are so cool (and you can't do that in Lua), it uses the LL blocking API for implementing the Blocking ZPOP! This kind of a thing only became possible recently (i.e. w/o serious hacking) via Experimental LL Key Space Notifications API BTW FYI.

Lastly, for keeping track over the interconnectedness of all things, the it uses [Rax](https://github.com/antirez/rax) (but still implements its own minimal linked list) :)

P.S. no fruits were harmed during the creation of this module.
P.S.S. honorable mention to the deprecated: https://github.com/RedisLabsModules/redex#zpop-key-withscore

## Commands

### `Z.POP <key>`
> Time complexity: O(log(N)) with N being the number of elements in the sorted set

Pops (remove and return) the lowest-ranking element from a sorted set.

**Return value:** Array, specifically the popped element's score and the popped element itself, or nil if key doesn't exist.

### `Z.REVPOP <key>`
> Time complexity: O(log(N)) with N being the number of elements in the sorted set

Pops (remove and return) the highest-ranking element from a sorted set.

**Return value:** Array, specifically the popped element's score and the popped element itself, or nil if key doesn't exist.

### `Z.BPOP <key> [<key> ...] <timeout>`
> Time complexity: O(log(N)) with N being the number of elements in the sorted set

Pops (remove and return) the lowest-ranking element from a sorted set. If the key doesn't exist, it blocks until `<timeout>` (given in milliseconds) is met. A value of 0 for the `<timeout>` means block indefinitely.

**Return value:** Array, specifically the popped key, the popped element's score and the popped element itself, or nil if the timeout is met.

### `Z.REVBPOP <key> [<key> ...] <timeout>`
> Time complexity: O(log(N)) with N being the number of elements in the sorted set

Pops (remove and return) the highest-ranking element from a sorted set. If the key doesn't exist, it blocks until `<timeout>` (given in milliseconds) is met. A value of 0 for the `<timeout>` means block indefinitely.

**Return value:** Array, specifically the popped key, the popped element's score and the popped element itself, or nil if the timeout is met.

# Building and running the module

## Build it

### Prerequirements

* Linux: `build-essential`
* MacOS: XCode, brew, ...

### Build steps

1. `git clone git@github.com:itamarhaber/zpop.git`
2. `cd zpop`
3. `make all -j 4`

## Test it

Prerequirements: Python, Python's setup tools and pip

1. `make test`

## Run it

Add the following line to your Redis conf file:

```
loadmodule /path/to/zpop/src/zpop.so
```

Or load the module from command line:

```
$ redis-server --loadmodule /path/to/zpop/src/zpop.so
```

Or load it with the `MODULE LOAD` command:

```text
127.0.0.1:6379> MODULE LOAD /path/to/zpop/src/zpop.so
OK
```

## License
BSD-3-Clause
