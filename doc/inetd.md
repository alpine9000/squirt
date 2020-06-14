# inetd configuration

If you use squirtd behind inetd you can have multiple concurrent sessions active.

## AmiTCP

1. Add squirt service to AmiTCP:db/services

    squirt           6969/tcp
 
2. Add squirtd to AmiTCP:db/inetd.conf

    squirt    stream tcp nowait root c:squirtd squirtd Work:Incoming/
    
3. Make sure inetd is included in AmiTCP:db/User-startnet

    run >NIL: AmiTCP:bin/inetd   

## Roadshow

1. Add squirtd to DEVS:Internet/services

    squirt           6969/tcp

2. Add squirtd to DEVS:Internet/servers

    squirt          stream                          c:squirtd Work:Incoming/


   
    
