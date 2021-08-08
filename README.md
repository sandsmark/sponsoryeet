Like https://github.com/stephen304/castblock but not CPU hungry bash hack.

Not actually working, just mDNS to search for and discover chromecasts + ssl connection done so far.

Still need some dummy basic http and json and shit.

Dependencies so far
-------------------

 * OpenSSL
 * protobuf


Why
---

Because the other solutions are either written in fucking crystal for no good
reason (why not just APL or REXX if you want to make it a pain for people to use
your code, and it just uses go-chromecast for the heavy lifting anyways) and
wants me to download a full distro in a docker container, are uglier hacks in
PHP, or are huge ugly resource intensive nodejs hacks with a metric shit-ton of
dependencies.

In short, the alternatives are huge, ugly, resource heavy cludges.
