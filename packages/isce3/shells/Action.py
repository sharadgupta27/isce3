#-*- coding: utf-8 -*-


# access the framework
import pyre


# pull {action} and rebrand it
class Action(pyre.action, family="isce3.cli"):
    """
    Protocol declaration for isce commands
    """


# end of file