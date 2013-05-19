Preface
=======

I should probably note that I'm currently broke 
and have no way to commission freelance technical writers 
to write a professional whitepaper for me right now.

That said, I'll attempt to cover the ideas and concepts
for the Fennec Overlay's underlying routing and switching,
and then illustrate plans for the early roadmap.

Introduction
============

The Fennec Overlay is a root-less distributed design 
to replace the Internet Protocol (IP) completely, eliminating centralized
address allocation, centralized domain naming, and bloated
bgp core routing tables, by eliminating address prefixes.

It also eliminated route prefix bloat by eliminating the need
for routing tables as currently concieved, using label switching
and kademlia routing along with public keys in place of ip addresses
to replace the current IP network architecture.

For a live demonstration of the label switching to be used, download CJDNS, 
compile, then join the IRC channel #cjdns at EFNet to connect to
Hyperboria and test the CJDNS network. For a good demonstration, attempt
some latency and bandwidth demanding applications such as SIP video conferencing,
first-person shooter video games or virtual world games, such as team fortress 2,
minecraft and jitsi, all of which work over linux.

Kademlia routing can be reasonably fast, see Telehash for a live testbed network 
that relies on that. CJDNS uses kademlia routing, at the time of writing, May 2013, 
but that network is constantly evolving, and they may use something better, now.

Although the focus of the Fennec Overlay is not on the physical line design, the
concept for Fennec (the internetwork) is that Fennec nodes run, not only 
in kernelmode within the host operating system's kernelmode network stack, but
also on every Fennec port's network interface chipset.

For example, if you plug in a USB Fennec interface dongle to your laptop, 
the Fennec network interface chipset within the dongle contains a minimal
Linux installation over an arm or mips core, running a Fennec overlay instance in 
device mode.
