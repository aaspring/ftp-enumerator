# FTP Enumerator

This is the source code for the FTP Enumerator used for the DSN 2016
peer-reviewed paper "FTP: The Forgotten Cloud". If you use this for
academic research, please cite our contributions as:
```
FTP: The Forgotten Cloud
Drew Springall, Zakir Durumeric, J. Alex Halderman
46th IEEE/IFIP International Conference on Dependable Systems and Networks
DSN '16, Toulouse, France, June 2016
```

This code is *not* being maintained. Feel free to fork and modify as you see
fit.

## Setup

1. Set the following values for your configuration
  * include/magicNumbers.h -- PASSWORD
    * Your contact e-mail
  * include/magicNumbers.h -- BAD_PORT_ARG
    * Format is same as PORT or PASV argument
  * include/magicNumbers.h -- USER_AGENT
    * What robots.txt useragent to follow

2. Make sure you have the external dependencies
  * [libevent](https://github.com/downloads/libevent/libevent/libevent-2.0.21-stable.tar.gz)
  * [jansson](http://www.digip.org/jansson/releases/jansson-2.7.tar.gz)
  * [SWIG](http://www.swig.org/)
  * Standard compiler chain such as gcc, make, etc
