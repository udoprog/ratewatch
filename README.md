Ratewatch is like Pipewatch, except it has the possibility to write the rates to a file instead of writing to tty (like pv).

This can be useful when wrapping programs that communicate over pipes, like sftp-server or something using (x)inetd to keep track of rates.

In the future, I plan for this to be pluggable (dbus perhaps?) to publish the rates on which the monitored programs are running.

Howto Use
===
Make:
---

    make

Test:
---

    cat /dev/urandom | ./rv test.log cat > /dev/null

Read log:
---

    tail -f test.log

For using with sftp:
---
    #!/bin/bash
    # A wrapper script for sftp-server, put in /usr/lib/sftp-server-mon and change the following in sshd_config;
    #
    # Subsystem sftp /usr/lib/openssh/sftp-server-mon
    #

    base=/var/log/sftp
    session=$base/$USER/$(date +'%y%m%d-%H%M%S')-$$

    mkdir -p $session
    echo $USER > $session/username
    touch $session/active

    function log {
        echo "$(date +'%Y-%m-%d %H:%M:%S') - $@" >> $session/main.log
    }

    log "login: $USER"
    /usr/lib/openssh/rv $session/main.log /usr/lib/openssh/sftp-server
    log "logout: $USER"

    rm $session/active
