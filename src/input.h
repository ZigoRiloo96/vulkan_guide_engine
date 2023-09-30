
struct
input
{
  enum
  action : u8
  {
    NONE = 0,
    LEFT = BIT(0),
    RIGHT = BIT(1),
    UP = BIT(2),
    DOWN = BIT(3)
  };

  struct
  state
  {
    u8 Hold{0};
    u8 PrevHold{0};
    u8 Down{0};
    u8 Up{0};
  };

  state Current;

  void PreUpdate()
  {
    Current.Hold = NONE;
  }

  void PushAction(action Action)
  {
    Current.Hold = Current.Hold | Action;
  }

  void PostUpdate()
  {
    Current.Down = (Current.Hold ^ Current.PrevHold) & Current.Hold;
    Current.Up = (Current.Hold ^ Current.PrevHold) & Current.PrevHold;
    Current.PrevHold = Current.Hold;
  }

  bool GetInputDown(action Action)
  {
    return Current.Down & Action;
  }

  bool GetInput(action Action)
  {
    return Current.Hold & Action;
  }

  bool GetInputUp(action Action)
  {
    return Current.Up & Action;
  }
};

static input g_Input;

