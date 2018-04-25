# Ze POP Redis Module

An educational [Redis](https://redis.io) module that implements ze [top requested](https://github.com/antirez/redis/issues/1861) Redis feature by users.

Has hopes (or the vanity) of becoming a part of ze core one day /cc @antirez ;)

Developed w/ <3 for RedisConf 18 and ze Redis community.

## Current status

Fresh out of development, and needs tests -> proof of concept.

## "Demo"

```text
redis:6379> ZADD z 0 a 1 b
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
| <- imagine that zis is a indefinitely blinking cursor
```

## Educational?!?

Yeah. It is originally intended as teaching/guiding aid for the newly-initiated Redis Module developer as part of the day 0 training at RedisConf 18. I felt I had to refresh upon the stale-ish 2yo HGETSET example, so that's basically ZPOP using the High Level Redis Module API.

But you don't use the HL API unless you know better, and I guess I know a little better, so there's the (better!) LL implementation that's actually being used by the module more "advanced" stuff.

Oh, it also shows how to inspect the command's name in order to implement both ZPOP and ZREVPOP efficiently.

Then, because blocking operations are so cool (and you can't do that in Lua), it uses the LL blocking API for implementing the Blocking ZPOP! This kind of a thing only became possible recently (i.e. w/o serious hacking) via Experimental LL Key Space Notifications API BTW FYI.

Lastly, for keeping track over the interconnectedness of all things, the it uses [Rax](https://github.com/antirez/rax) (but still implements its own minimal linked list) :)

P.S. no fruits were harmed during the creation of this module.

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

## License
BSD-3-Clause