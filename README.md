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

 * OpenSSL
 * protobuf

Has a simple HTTP implementation, mdns implementation and uses c++11 regexes
for parsing the json, so no more deps.

Could implement protobuf myself as well, but meh.

Why
---

Because the other solutions are either written in fucking crystal for no good
reason (why not just APL or REXX if you want to make it a pain for people to use
your code, and it just uses go-chromecast for the heavy lifting anyways) and
wants me to download a full distro in a docker container, are uglier hacks in
PHP, or are huge ugly resource intensive nodejs hacks with a metric shit-ton of
dependencies.

In short, the alternatives are huge, ugly, resource heavy cludges.
