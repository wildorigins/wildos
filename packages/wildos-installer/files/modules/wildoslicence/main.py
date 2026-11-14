#!/usr/bin/env python3
"""Refuses to install unless the licence was accepted.

The Licence page keeps Continue disabled until its box is ticked, and
writes the answer to global storage as wildosLicenceAccepted. This job runs
first, before the partition jobs, so even a way past the page that nobody
has thought of ends here with the disk untouched.
"""

import libcalamares


def pretty_name():
    return "Checking the licence was accepted"


def run():
    if libcalamares.globalstorage.value("wildosLicenceAccepted") is True:
        return None
    return ("The licence was not accepted",
            "Wild OS installs only after the box on the first page is ticked. "
            "Nothing on this computer has been changed.")
