import math as math
import matplotlib.pyplot as plt


# Press Shift+F10 to execute it or replace it with your code.
# Press Double Shift to search everywhere for classes, files, tool windows, actions, and settings.

class SpringTracker:
    def __init__(self):
        self.Vel = 0.0
        self.Mass = 0.5
        self.StepTime = 0.01

    def TrackOneFrame(self, CurrVal, TargetVal, SpringConst, InDeltaTime):
        DampingCoef = math.sqrt(4.0 * self.Mass * SpringConst)

        NewVal = CurrVal
        while InDeltaTime > 0.0:
            Dt = min(self.StepTime, InDeltaTime)
            Force = -SpringConst * (NewVal - TargetVal) - DampingCoef * self.Vel
            Acc = Force / self.Mass
            NewVal += (self.Vel + Acc / 2.0 * Dt) * Dt
            self.Vel += Acc * Dt
            InDeltaTime = max(InDeltaTime - Dt, 0.0)

        return NewVal


def Track():
    DeltaTime = 1 / 30.0

    CurrVal = 0.0
    TargetVal = 10.0

    ResX = list()
    ResY = list()
    ResX.append(0.0)
    ResY.append(CurrVal)

    SpringConst = 4.0

    tracker = SpringTracker()
    for i in range(100):
        CurrVal = tracker.TrackOneFrame(CurrVal, TargetVal, SpringConst, DeltaTime)
        ResX.append((i + 1) * DeltaTime)
        ResY.append(CurrVal)

    return ResX, ResY


# Press the green button in the gutter to run the script.

ResX, ResY = Track()
plt.scatter(ResX, ResY)
plt.show()