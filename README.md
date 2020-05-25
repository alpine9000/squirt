# squirt - transfer files from Unix to Amiga with no security

Push files quickly from a modern computer to an Amiga. squirtd is designed to be super small, use hardly any ram and super fast.

Please don't run squirtd on any computer that is connected to the open internet. It's insecure and allows anyone to write files directly to the computer.

## Usage

squirtd requires a destination folder argument where it will write any files that are squirted it's way.

    squirtd <destination folder>

## Running as a daemon

It's easy to run squirtd on your Amiga as a background daemon, just start it from your TCP/IP stack's startup script. squirtd should gracefully exit when your TCP/IP stack exits.

### AmiTCP
Add the following to AmiTCP:db/User-Startnet.

    run >NIL: aux:squirtd aux:
    
where `aux:` is the destination folder you want squirtd to write files.
    
### Roadshow
Add the following to S:Network-Startup.

    run >NIL: aux:squirtd aux:

where `aux:` is the destination folder you want squirtd to write files.

## squirting a file

See below for a demo of squirting a 5mb file to my real Amiga 500 with an XSurf-500

![](demo.gif)
