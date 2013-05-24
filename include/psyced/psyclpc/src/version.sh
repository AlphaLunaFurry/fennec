# This file defines a number of shell variables to describe the projects
# version. It is meant to be modified by the bumpversion script.

# The checkin date
version_date=$(echo "\$Date: 2009-01-12 21:08:20 -0700 (Mon, 12 Jan 2009) $" | sed -e 's/[$]Date: \([0-9]*-[0-9]*-[0-9]*\).*\$/\1/')

# The checkin time
version_time=$(echo "\$Date: 2009-01-12 21:08:20 -0700 (Mon, 12 Jan 2009) $" | sed -e 's/[$]Date: \([0-9]*-[0-9]*-[0-9]*\) \([0-9]*:[0-9]*:[0-9]*\).*\$/\2/')

# The checkin revision
version_revision=$(echo "\$Revision: 2483 $" | sed -e 's/[$]Revision: \([0-9][0-9]*\) *\$/\1/')

# The version type: dev, stable, maintenance, release
version_type="stable"
version_longtype="stable"

# A timestamp, to be used by bumpversion and other scripts.
# It can be used, for example, to 'touch' this file on every build, thus
# forcing revision control systems to add it on every checkin automatically.
version_stamp="Mon Aug 29 16:41:20 CEST 2011"

# Okay, LDMUD is using 3.x.x so to avoid conflicts let's just use 4.x.x
version_major=4
version_minor=0
version_micro=14

