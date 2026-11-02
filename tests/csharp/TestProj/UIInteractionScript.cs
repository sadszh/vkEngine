using vkEngine.EngineCore;

namespace TestProj;

public sealed class UIInteractionScript : EntityScript
{
    private Canvas canvas = null!;
    private UIButton probe = null!;
    private UIText enableLabel = null!;
    private UIText pointerState = null!;
    private UIText eventLog = null!;
    private UIImage frontLayer = null!;
    private UIText hitCounts = null!;
    private UIText hitMode = null!;
    private UIContainer? modal;
    private UIButton? resumeButton;
    private UIButton? exitButton;
    private readonly Queue<string> recentEvents = new();
    private readonly List<(UIComponent Node, Action<UIPointerEvent> Handler)> clicks = new();
    private readonly List<(UIComponent Node, Action<UIPointerEvent> Handler)> observed = new();
    private int probeClicks;
    private int backClicks;
    private int frontClicks;
    private int mode;

    public UIInteractionScript(UInt32 entity) : base(entity) { }

    public override void Start()
    {
        canvas = GetComponent<Canvas>()!;
        var board = canvas.Find("Board")!;
        var events = canvas.Find("Events", board)!;
        probe = (UIButton)canvas.Find("Probe", events)!;
        var enable = canvas.Find("EnableProbe", events)!;
        enableLabel = (UIText)canvas.Find("Label", enable)!;
        pointerState = (UIText)canvas.Find("PointerState", events)!;
        eventLog = (UIText)canvas.Find("EventLog", events)!;
        observe(probe, "Probe");
        bindClick(enable, _ => toggleProbe());

        var layout = canvas.Find("Layout", board)!;
        observe(canvas.Find("RotatingPlate", canvas.Find("Spinner", layout))!, "Rotated");
        observe(canvas.Find("Mirrored", layout)!, "Mirrored");

        var raycast = canvas.Find("Raycast", board)!;
        frontLayer = (UIImage)canvas.Find("FrontLayer", raycast)!;
        hitCounts = (UIText)canvas.Find("HitCounts", raycast)!;
        hitMode = (UIText)canvas.Find("HitMode", raycast)!;
        bindClick(canvas.Find("BackTarget", raycast)!, _ => { ++backClicks; updateHitCounts(); });
        bindClick(frontLayer, _ => { ++frontClicks; updateHitCounts(); });
        bindClick(canvas.Find("CycleHitMode", raycast)!, _ => cycleHitMode());

        var lifecycle = canvas.Find("Lifecycle", board)!;
        bindClick(canvas.Find("PauseMenu", lifecycle)!, _ => openPauseMenu());
        bindClick(canvas.Find("PrefabAgain", lifecycle)!, _ =>
            SceneManager.InstantiatePrefab(1024, NVec3.Zero, NQuat.Identity, new NVec3(1, 1, 1)));
    }

    public override void Update()
    {
        if (UIShowcaseScript.IsAutoLayoutOpen) return;
        if (Input.IsKeyPressed(KeyCode.D)) toggleProbe();
        if (Input.IsKeyPressed(KeyCode.M)) openPauseMenu();
    }

    public override void Unload()
    {
        foreach (var (node, handler) in clicks) node.Click -= handler;
        foreach (var (node, handler) in observed)
        {
            node.PointerEnter -= handler;
            node.PointerExit -= handler;
            node.PointerDown -= handler;
            node.PointerUp -= handler;
            node.PointerCancel -= handler;
            node.Click -= handler;
        }
        clicks.Clear();
        observed.Clear();
        if (resumeButton != null) resumeButton.Click -= resume;
        if (exitButton != null) exitButton.Click -= exit;
    }

    private void bindClick(UIComponent node, Action<UIPointerEvent> handler)
    {
        node.Click += handler;
        clicks.Add((node, handler));
    }

    private void observe(UIComponent node, string name)
    {
        Action<UIPointerEvent> handler = e => recordEvent(name, e);
        node.PointerEnter += handler;
        node.PointerExit += handler;
        node.PointerDown += handler;
        node.PointerUp += handler;
        node.PointerCancel += handler;
        node.Click += handler;
        observed.Add((node, handler));
    }

    private void recordEvent(string name, UIPointerEvent e)
    {
        if (e.Target.Handle == probe.Handle && e.Type == UIPointerEventType.Click) ++probeClicks;
        recentEvents.Enqueue($"{name}: {e.Type} @ {e.Position.x:0}, {e.Position.y:0}");
        if (recentEvents.Count > 4) recentEvents.Dequeue();
        eventLog.Text = string.Join('\n', recentEvents);
        pointerState.Text = $"Hover: {e.Target.IsHovered} | Press: {e.Target.IsPressed} | Clicks: {probeClicks}";
    }

    private void toggleProbe()
    {
        probe.Interactable = !probe.Interactable;
        enableLabel.Text = probe.Interactable ? "Disable [D]" : "Enable [D]";
    }

    private void updateHitCounts() => hitCounts.Text = $"Back clicks: {backClicks} | Front clicks: {frontClicks}";

    private void cycleHitMode()
    {
        mode = (mode + 1) % 4;
        frontLayer.HitTestEnabled = mode != 0;
        frontLayer.Interactable = mode == 3;
        frontLayer.Color = new NVec4(0.9f, 0.35f, 0.12f, mode == 2 ? 0 : 0.65f);
        hitMode.Text = mode switch
        {
            0 => "Mode 1: pass through",
            1 => "Mode 2: disabled layer blocks input",
            2 => "Mode 3: alpha 0 still blocks input",
            _ => "Mode 4: image receives Click"
        };
    }

    private void openPauseMenu()
    {
        // The last root covers the board, and this Canvas now covers the other Canvases too.
        canvas.SortOrder = 100;
        modal = canvas.CreateContainer("PauseModal");
        modal.RectTransform = new RectTransform { AnchorMax = new NVec2(1, 1), Pivot = NVec2.Zero };
        modal.HitTestEnabled = true;
        modal.Interactable = false;
        var shade = canvas.CreateImage("Shade", modal);
        shade.RectTransform = new RectTransform { AnchorMax = new NVec2(1, 1), Pivot = NVec2.Zero };
        shade.Color = new NVec4(0.01f, 0.02f, 0.04f, 0.82f);

        var panel = canvas.CreateImage("Panel", modal);
        panel.RectTransform = new RectTransform
        {
            AnchorMin = new NVec2(0.5f, 0.5f), AnchorMax = new NVec2(0.5f, 0.5f),
            OffsetMin = new NVec2(-310, -150), OffsetMax = new NVec2(310, 150), Pivot = NVec2.Zero
        };
        panel.Color = new NVec4(0.055f, 0.12f, 0.17f, 1);
        var message = canvas.CreateText("Message", panel);
        message.RectTransform = new RectTransform
        {
            OffsetMin = new NVec2(24, 24), OffsetMax = new NVec2(596, 198)
        };
        message.FontSize = 23;
        message.WordWrap = true;
        message.Text = "SIMULATION PAUSED\nUpdate is stopped. UI events still run.\nThe non-interactable parent blocks input; its child buttons remain interactive.";
        resumeButton = createModalButton("Resume", "Resume simulation", panel, 24, 278);
        exitButton = createModalButton("Exit", "Exit showcase", panel, 322, 274);
        resumeButton.Click += resume;
        exitButton.Click += exit;
        EngineStateManager.SetState(EngineState.Paused);
    }

    private UIButton createModalButton(string name, string caption, UIComponent parent, float x, float width)
    {
        var button = canvas.CreateButton(name, parent);
        button.RectTransform = new RectTransform
        {
            OffsetMin = new NVec2(x, 220), OffsetMax = new NVec2(x + width, 274), Pivot = NVec2.Zero
        };
        button.Color = new NVec4(0.08f, 0.35f, 0.4f, 1);
        button.HoverColor = new NVec4(0.12f, 0.6f, 0.6f, 1);
        button.PressedColor = new NVec4(0.03f, 0.2f, 0.25f, 1);
        var label = canvas.CreateText("Label", button);
        label.RectTransform = new RectTransform
        {
            AnchorMax = new NVec2(1, 1),
            OffsetMin = new NVec2(12, 4), OffsetMax = new NVec2(-12, -4)
        };
        label.LayoutOptions = new TextLayoutOptions
        {
            FontSize = 20, HorizontalAlignment = TextHorizontalAlignment.Center,
            VerticalAlignment = TextVerticalAlignment.Middle, WordWrap = true
        };
        label.Text = caption;
        return button;
    }

    private void resume(UIPointerEvent e)
    {
        // Removing the parent inside its child's Click releases all modal subscriptions.
        canvas.Remove(modal!);
        modal = null;
        resumeButton = null;
        exitButton = null;
        canvas.SortOrder = 0;
        EngineStateManager.SetState(EngineState.Running);
    }

    private void exit(UIPointerEvent e) => EngineStateManager.SetState(EngineState.Terminated);
}
