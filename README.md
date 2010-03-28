Howto Use
===
Make:
---

    make

Test:
---

    cat /dev/urandom | rv test.log > /dev/null

Read log:
---

    cat test.log

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
