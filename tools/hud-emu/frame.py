import os
ROOT=os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','..'))
import sys; sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from emu3 import *
def hud_frame(e,n):
    """replicate the HUD/panel part of the per-frame routine (0x112cc..0x113f6) for frame counter n"""
    c=e.call; log=[]
    def go(a):
        err=c(a); log.append((hex(a),err))
    g8ef=e.rb(0x4a8ef); g8ef=g8ef-256 if g8ef>127 else g8ef
    for a in (0x35238,0x3548a,0x35cf2,0x37d78,0x35bf6,0x35a4c): go(a)
    if g8ef>0 or (n&3)>1: go(0x3697a); go(0x36a10)
    if g8ef>0 or (n&3)<2: go(0x36c3c)
    if g8ef>0 or (n&3)<2 or e.rl(0x4ab1e)>-0x8000: go(0x36ade)
    if g8ef>0 or (n&7)<2: go(0x36bee)
    if g8ef>0 or (n&15)<2: go(0x353ec)
    for a in (0x34b1a,0x3736c,0x354d4,0x36c96,0x36d24,0x36590,0x35808,0x36528,0x36db2,0x35630): go(a)
    if g8ef>=0: e.b(0x4a8ef,(g8ef-1)&255) if g8ef>0 else None
    return log
