"""Behavioral model tests for the single-slip-switch rules."""
from dataclasses import dataclass
from enum import IntEnum

class Direction(IntEnum):
    CLOSE = -1
    STOP = 0
    OPEN = 1

@dataclass
class Model:
    motion: Direction = Direction.STOP
    blocked: Direction = Direction.STOP
    switch: bool = False

    def drive(self, direction: Direction) -> bool:
        if self.blocked != Direction.STOP and direction != -self.blocked:
            return False
        self.motion = direction
        return True

    def trip(self) -> None:
        self.switch = True
        self.blocked = self.motion
        self.motion = Direction.STOP

    def release(self) -> None:
        self.switch = False
        self.blocked = Direction.STOP

def test_trip_blocks_same_direction():
    m = Model()
    assert m.drive(Direction.OPEN)
    m.trip()
    assert not m.drive(Direction.OPEN)
    assert m.drive(Direction.CLOSE)

def test_release_restores_both_directions():
    m = Model()
    m.drive(Direction.CLOSE)
    m.trip()
    assert m.drive(Direction.OPEN)
    m.release()
    assert m.drive(Direction.CLOSE)

def test_stop_is_immediate_on_trip():
    m = Model()
    m.drive(Direction.OPEN)
    m.trip()
    assert m.motion == Direction.STOP
