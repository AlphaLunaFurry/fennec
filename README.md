Fennec Overlay
==============

The Fennec Overlay, a rootless and utterly decentralized Internet Protocol replacement.


Concept
=======

The basic concept of the Fennec Overlay, despite the name, is to replace the Internet Protocol completely.

The reason for calling it an overlay is due to the fact that building infrastructure 
on the same scale as the Internet is ridiculously expensive, and so tunneling over the Internet 
in the initial phases of testing and deployment is a critically vital strategy.

Replacing all 7 OSI Layers
--------------------------

The Fennec Overlay's ultimate and final goal is to redesign and rebuild the shiny new Internet
protocol stack from the physical network interface cards all of the way up 
to application layer protocols, in other words, it is not merely an overlay.

It is an entire complete protocol stack.

Yo Dawg I Herd U Liek Networks
------------------------------

Obviously, the Fennec Overlay, by necessity, will need to have a very sophisticated overlay
design and architecture to tunnel over the existing Internet Protocol infrastructure and protocols.

By this, I mean UDP with elaborate hole-punching.

Eventually, however, it will be the other way around, with Fennec carrying 
legacy IPv4 and IPv6 packets over it similarly to carrier ethernet or MPLS is used for today.

General Design Principles
-------------------------

First of all, the primary goal for the Fennec Overlay is to abolish the distinction 
between MAC and IP addresses. Instead, all identities will be referred to by way of ECC public keys.
This does away with centrally allocated IP address blocks and the lack of globally-unique MAC addressing.
Centrally-assigned autonomous system numbers are also discarded, as all network administrative domains
from residential home networks to huge international carrier networks are identified by ECC public keys.

Secondly, the globally unique human-readable naming system referred to as the domain name system 
will eventually be discarded as well in favor of a petname system.
For the purposes of comfort and smooth transition, an alternative namecoin blockchain with a very high
difficulty will be used to allocate top-level domains, while second-level and below use traditional
name servers such as BIND and PowerDNS. To be fully compatible with the new ECC identifier system,
new DNS records are required, and can easily be added to existing traditional DNS name server software.

Thirdly, there is no distinction between routing and switching layers. Label switching is used 
at the hardware layer, such as within network interface card or home gateway chipsets, but
label switching circuits need to be built, first. This means routing. Routing is conducted via
a Kademlia-style routing algorithm. 

This Project
------------

For the reference implementation and proof of concept, I, Alex Maurin (AKA Coyo), will use Python.

The overlay networking will use TeleHash, ZeroMQ, MsgPack, Twisted, SQLAlchemy and ProtoBuf.

The cryptography will largely be handled by NaCl, GNU Privacy Guard, Off-The-Record and OpenSSL.

The demonstration application will use CMake, wxWidgets, possibly GLADE, and for Windows ports, Py2Exe, 
NSIS and the BattleTorrent plug-in for NSIS.


