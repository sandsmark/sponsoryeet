![logo](/yeet.png)

sponsoryeet
===========

![screenshot](/screenshot.png)

Automatically yeets sponsors away from the chromecast.

Like https://github.com/stephen304/castblock but not CPU hungry bash hack
depending on an endless amount of supply chain attacks waiting to happen (aka.
an average go project).


Dependencies
------------

 * C++ compiler
 * OpenSSL
 * protobuf

Has a simple HTTP implementation, mdns implementation and uses c++11 regexes
for parsing the json, so no more deps.

Could implement protobuf myself as well, but meh.


Building
--------

Either just run `make`, or use cmake (probably better if you're not using a
proper operating system).

Usage
-----

Run "sponsoryeet" (from a terminal).

It will automatically discover any chromecast on your network (better than any
other chromecast app I've seen, including Chrome), connect, wait for youtube to
start playing and download a list of segments to skip, and skip them when they
occur.

You can also pause/resume the video by applying pressure to your spacebar, and
quit by pressing q or escape.

If you launch it with `-v` or `--verbose` it will print a lot of debug output.


Ad-block
--------

If you launch it with `-a` or `--adblock` it will try to skip ads.

It will try to do so by stopping the video, and re-open it at one second past
where it was.

I have Premium so I haven't been able to test it, but it should work in theory.
I implemented it because youtube bugs out sometimes and shows me ads anyways,
but it happens intermittently so I can't really test it.

Why
---

Because the other solutions are either written in fucking crystal for no good
reason (why not just APL or REXX if you want to make it a pain for people to use
your code, and it just uses go-chromecast for the heavy lifting anyways) and
wants me to download a full distro in a docker container, are uglier hacks in
PHP, or are huge ugly resource intensive nodejs hacks with a metric shit-ton of
dependencies.

In short, the alternatives are huge, ugly, resource heavy cludges.
