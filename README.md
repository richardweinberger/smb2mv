# `smb2mv` - Perform a Server Side File Move on Windows Shares

The main use case of this tool is moving files between different shares
in the same SMB server.
Usually, when a command like `mv /mnt/share1/foo /mnt/share2/bar` is executed,
the CIFS client does a full network copy of `foo`, although both shares are on
the same server.
`smb2mv` avoids this overhead and first performs a server-side copy (`FSCTL_SRV_COPYCHUNK`),
followed by an unlink.

## Effectiveness

	rw@box:~> time smb2mv /mnt/share1/1gb.dat /mnt/share2/

	real    0m5,611s
	user    0m0,001s
	sys     0m0,077s

	rw@box:~> time mv /mnt/share1/1gb.dat /mnt/share2/

	real    1m17,250s
	user    0m0,005s
	sys     0m1,688s


## Usage

	smb2mv SOURCE DESTINATION

- Both source and destination have to be on the same server.
- destination can either be a file or a directory.

## TODO

- Add verbose and overwrite modes, `-v` and `-i`.
- Support multiple source files/directories.
- Integrate in coreutils' `mv` utility.
