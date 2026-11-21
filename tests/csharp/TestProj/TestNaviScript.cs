using vkEngine.EngineCore;
namespace TestProj;

public sealed class TestNaviScript(uint entity) : EntityScript(entity)
{
    private NavMeshAgent agent = null!;
    private Transform transform = null!;
    private readonly NVec3 extent = new(1, 3, 1);
    private NavPath path = new(NavPathStatus.Failed, []);
    private int waypoint;

    public override void Start()
    {
        agent = GetComponent<NavMeshAgent>()!;
        transform = GetComponent<Transform>()!;
        Input.CursorMode = CursorMode.Normal;
        Console.WriteLine("Left-click the floor to move; Escape to exit.");
    }

    public override void Update()
    {
        if (Input.IsMouseButtonPressed(MouseButton.Left))
            SetClickedDestination();
        float step = Time.DeltaTime * 3;
        while (waypoint < path.Points.Length && step > 0)
        {
            var delta = path.Points[waypoint] - transform.LocalPosition;
            float distance = MathF.Sqrt(delta.x*delta.x + delta.y*delta.y + delta.z*delta.z);
            if (distance <= step)
            {
                transform.LocalPosition = path.Points[waypoint++];
                step -= distance;
            }
            else
            {
                transform.LocalPosition += delta * (step / distance);
                break;
            }
        }
        if (Input.IsKeyDown(KeyCode.Escape)) EngineStateManager.SetState(EngineState.Terminated);
    }

    private void SetClickedDestination()
    {
        if (!Input.TryGetMouseRay(out var origin, out var direction) || direction.y >= -0.0001f)
            return;
        // The sample's walkable floors lie on the world-space Y=0 plane.
        float distance = -origin.y / direction.y;
        if (distance < 0)
            return;
        var nextPath = agent.FindPath(origin + direction * distance, extent);
        if (nextPath.Status != NavPathStatus.Complete)
        {
            Console.WriteLine("Destination is not reachable.");
            return;
        }
        path = nextPath;
        waypoint = 0;
    }
}
