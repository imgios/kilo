<h1>
  <sup><sub><code>abc|</code></sub></sup> kilo
</h1>
  
`kilo` is the [antirez's kilo](http://antirez.com/news/108) text editor built from scratch as result of the [`Build Your Own Text Editor` tutorial](https://viewsourcecode.org/snaptoken/kilo/), useful to get more in depth with C progamming and its concepts.

###### Prerequisites

- C compiler
- Make

> `sudo apt install gcc make`

###### Usage

Clone the repostiory and compile it:

- Using `make`: run `make` in the src/ project directory
- Using C compiler: `cc kilo.c -o kilo`

Then run `./kilo [<filename>]`.
