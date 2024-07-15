# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(custom-2-1) begin
(custom-2-1) This thread should have priority 33.  Actual priority: 33.
(custom-2-1) This thread should have priority 35.  Actual priority: 35.
(custom-2-1) acquire2: got the lock
(custom-2-1) acquire2: set priority to 32.  Effective priority should be 33. Actual effective priority: 33.
(custom-2-1) acquire1: got the lock
(custom-2-1) acquire1: done
(custom-2-1) acquire2: My priority should now be 32, my actual priority is 32.
(custom-2-1) acquire2: done
(custom-2-1) acquire1, acquire2 must already have finished, in that order.
(custom-2-1) This should be the last line before finishing this test.
(custom-2-1) end
EOF
pass;
