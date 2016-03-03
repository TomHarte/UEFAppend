# UEFAppend
A command-line tool for appending Acorn Atom ATM files to UEF files.

Usage:
`_uefappend target.uef {file.atm}_`

# Limitations
This tool does not yet understand compressed UEF files; it therefore cannot append to almost every UEF file currently in existence. This will be an easy fix once all other issues are resolved and it becomes acceptable to add a zlib dependency.

As a workaround for ordinary output tasks, use gzip to compress the product of this tool:

    gzip target.uef
    mv target.uef.gz target.uef
