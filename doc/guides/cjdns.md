TODO
----

Write a detailed guide and handbook for CJDNS and how to connect to
I2P public nodes manually until the glue code is reasonable good at
automagic connection negotiation.


Fair Warning
============

CJDNS is rapidly evolving, and the design of Fennec relies heavily on the security strengh
of CJDNS, which in turn relies on NaCl, which is an experimental high-speed
ECC cryptographic library that has NOT been thoroughly battle-hardened like OpenSSL.

However, nothing says you have to trust CJDNS completely. Nothing stops you 
from using OpenSSL over CJDNS just in case.

The easiest way to do this might be to deploy a private independent Tor network
using the recently-introduced IPv6 support (experimental) and the ability to use
alternative Directory Authorities and bootstrap into a "mini-Tor" within CJDNS.

I am also not terribly confident that the Friend-to-Friend model is really that great.

It sounds good on paper, but if only one of your friends is forced at gunpoint or via
extortion and blackmail to divulge his private openpgp keys, your entire network is PWNT.

To thwart this (to some extent) you can connect to a much more extensive CJDNS network, and
ASSUME that it is already compromised with federal agents, the KGB, MI6, whatever.

So you use a private Tor network to make their jobs difficult. :D
