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
    # A simple sftp wrapper

    /usr/lib/openssh/rv /var/log/sftp/rate.$USER.in |\
      /usr/lib/openssh/sftp-server |\
      /usr/lib/openssh/rv /var/log/sftp/rate.$USER.out
