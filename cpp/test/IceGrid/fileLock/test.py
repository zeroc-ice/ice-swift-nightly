# Copyright (c) ZeroC, Inc.


import sys
from IceGridUtil import IceGridRegistryMaster, IceGridTestCase
from Util import TestSuite


class IceGridAdminTestCase(IceGridTestCase):
    def runClientSide(self, current):
        sys.stdout.write("testing IceGrid file lock... ")
        registry = IceGridRegistryMaster(portnum=25, ready="", quiet=True)
        registry.start(current)
        registry.expect(current, ".*IceInternal::FileLockException.*")
        registry.stop(current, False)
        print("ok")


TestSuite(
    __file__,
    [IceGridAdminTestCase(application=None)],
    runOnMainThread=True,
    multihost=False,
)
