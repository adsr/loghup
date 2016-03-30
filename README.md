loghup
======

    loghup version 0.1

    Usage: loghup [options] -- <command>

    Options:
        -h         Show this help
        -o <path>  Log stdout to <path>
        -e <path>  Log stderr to <path>
        -s <sig#>  Trap <sig#> instead of SIGHUP (1)
        -f         Forward signal to child process

loghup execs `<command>` and captures its stdout and stderr at the paths
specified by `-o` and `-e` respectively. When loghup receives a SIGHUP (or other
signal specified by `-s`), it closes and reopens log handles (useful for log
rotation). If `-f` is specified, the signal is forwarded to the child process.

Note that is mainly useful for legacy apps that don't manage their own logging
and/or only write to stdout/stderr. If you are writing something from scratch,
consider using something like log4j.

Also note that there is a small overhead for copying log data over pipes that
one could avoid entirely by logging directly to disk from your process.

Nevertheless, see `TODO` for some ideas that may make this program useful in
other scenarios.

# Build

    $ make
    $ make install # optional

# TODO

* Ability to open extra file descriptors for child proc to write to, e.g.,
  stddebug, stdlol, etc.
* A read-from-stdin mode
* An option to tee to stdout/stderr in addition to log files
* An option to disable signal trapping
* Try the `splice` syscall instead of `read`/`write` if available
