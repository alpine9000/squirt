# squirt - Remotely manage your Amiga over TCP/IP from modern systems

`squirtd` is a small server (the executable is less than 5kb) that lets you remotely manage your Amiga over TCP/IP using set of command lines tools running on a modern system.

You can:
 * Send (squirt) files
 * Receive (suck) files
 * Run non interactive commands
 * List directories
 * Perform incremental backups
 * Run a remote cli
     * Filename completion on remote Amiga files
     * Line editing and command history (similar to bash etc)
     * Run (non interactive) commands remotely on your Amiga
     * Run commands locally on your modern system
     * Local commands can easily access both Amiga and local files (even mixed in the same command)

:radioactive::warning::no_entry: Please don't run `squirtd` on any computer that is connected to the open internet!!! :no_entry::warning::radioactive:

There are no passwords, no server side validations and transfers are plain text. To top it off it's written in C and the server code has `goto` statements! :stuck_out_tongue_closed_eyes:

## Usage

`squirtd` requires a destination folder argument where it will write any files that are squirted it's way.

    squirtd destination_folder

Note: `destination folder` must end with a valid directory separator character as the filename is simply appended to `destination folder`.

For example:

    squirtd Work:Incoming/

## Running as a daemon

It's easy to run `squirtd` on your Amiga as a background daemon, just start it from your TCP/IP stack's startup script. `squirtd` should gracefully exit when your TCP/IP stack exits.

### AmiTCP
Add the following to AmiTCP:db/User-Startnet.

    run >NIL: aux:squirtd Work:Incoming/

where `Work:Incoming/` is the destination folder you want `squirtd` to write files.

### Roadshow
Add the following to S:Network-Startup.

    run >NIL: aux:squirtd Work:Incoming/

where `Work:Incoming/` is the destination folder you want `squirtd` to write files.

## Management commands

### squirting a file

    squirt hostname filename

![](images/squirt.png)

### sucking a file

    squirt_suck hostname filename

![](images/suck.png)

### running a command

    squirt_exec hostname command and arguments

![](images/exec.png)

### remote cli
    squirt_cli hostname
    
By default any command you type will be executed on the remote Amiga:
    
    1.WB3.1>dir DEVS:Monitors
    DblPAL                           DblPAL.info
    Multiscan                        Multiscan.info
    PAL                              PAL.info
    Super72                          Super72.info
    VGAOnly                          VGAOnly.info
    
If you prefix the command with `!` the command will be executed on the host (local) computer
    
    1.WB3.1:> !uname
    Darwin

For commands that are run locally, any filenames you specify will be transferred to the local machine so that the local command can access them.

    1.WB3.1:> !emacs S:Startup-Sequence 
  
If the command modifies the file, it will be saved back to the Amiga once the command exits.

You can mix local and remote files with local commands. You indicate a file as local by prefixing it with an `!`. Also any file that starts with a `~` will also be treated as a local file. Arguments starting with `-` are passed directly to local commands. If arguments to local commands do not start with `-` they must be escaped with `!` to indicate that are not remote files.

    1.WB3.1:> !cp S:Startup-Sequence ~/Startup-Sequence.backup
    1.WB3.1:> !echo !";A new line" >> S:Startup-Sequence
    1.WB3.1:> type S:Startup-Sequence
    ...
    ...
    Resident Execute REMOVE
    Resident Assign REMOVE

    C:LoadWB
    EndCLI >NIL:
    ;A new line
    1.WB3.1:> !diff S:Startup-Sequence ~/Startup-Sequence.backup 
    67d66
    < ;A new line
    1.WB3.1:> 
    
Click on the image below to see a demo video of the remote shell in action.
[![squirt_cli demo video](https://img.youtube.com/vi/n2cS01OXowc/0.jpg)](https://www.youtube.com/watch?v=n2cS01OXowc)

### backing up

    squirt_backup [--skipfile=skip_filename] hostname path_to_backup
 
`skip_filename` is an optional file which includes a list of files or directories that should not be backed up.

![](images/backup.png)

### list directory

    squirt_dir hostname path

![](images/dir.png)


## License

Unless otherwise specified in the source file, all files are Copyright &copy; 2020 Enable Software Pty Ltd. All Rights Reserved.

This software is free software. You can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 3 of the License, or (at your option) any later version.

This software is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

You should have received a copy of the GNU General Public License along with the software; see the file LICENSE.
