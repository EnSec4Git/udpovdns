udpovdns
========

A simple UDP over DNS implementation, containing a sample server
and client.

Requirements
========

In order to compile the client application, the user needs `g++` (not
`gcc`!) and make.

In order to run the client application (or anything using the DNS
implementation of standard networking routines), Perl is needed.
(To be removed)

The server-side depends on Python 2.7+ and the Twisted framework.

NOTE
--------

The server component requires an installation of the Twisted framework,
version >= 13.2.0. This is needed due to the usage of the
`adoptDatagramPort()` function, that is available in a specific reactor,
and reactor selection is available only in newer versions of Twisted.
