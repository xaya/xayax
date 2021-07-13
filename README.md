# Xaya X

Most of the Xaya platform (in particular,
[libxayagame](https://github.com/xaya/libxayagame)) is [blockchain
agnostic](https://xaya.medium.com/xaya-blockchain-agnostic-polygon-b0b3d29cccb3)
and needs only very basic functionality of the underlying base chain.
Traditionally, the base chain for Xaya applications has been
[Xaya Core](https://github.com/xaya/xaya), but it is possible to run
Xaya GSPs with other base chains as well to expand the reach of the Xaya
platform.

Xaya X provides the tools to run an *interface layer* based
on another blockchain, but which exposes a [Xaya-Core-like
interface](https://github.com/xaya/xaya/blob/master/doc/xaya/interface.md)
so that GSPs built with libxayagame can run out-of-the-box on that
base chain.
