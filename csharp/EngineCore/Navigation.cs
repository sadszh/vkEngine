namespace vkEngine.EngineCore
{
    public enum NavPathStatus { Failed, Complete, Partial }
    public sealed record NavPath(NavPathStatus Status, NVec3[] Points);
    public readonly record struct NavRaycastHit(bool Blocked, NVec3 Position, NVec3 Normal);

    public static unsafe class Navigation
    {
        internal const int MaxPoints = 4096;
        public static NavPath FindPath(uint navMeshId, NVec3 start, NVec3 end, NVec3 extent)
        {
            var points = new NVec3[MaxPoints];
            int count = 0;
            NavPathStatus status;
            fixed (NVec3* buffer = points)
                status = (NavPathStatus)NativeFunctionRegistry.Functions.NavFindPath(navMeshId, &start, &end, &extent, buffer, points.Length, &count);
            Array.Resize(ref points, count);
            return new(status, points);
        }
        public static bool SamplePosition(uint navMeshId, NVec3 position, NVec3 extent, out NVec3 point)
        {
            NVec3 result = default;
            bool success = NativeFunctionRegistry.Functions.NavSamplePosition(navMeshId, &position, &extent, &result) != 0;
            point = result;
            return success;
        }

        public static bool Raycast(uint navMeshId, NVec3 start, NVec3 end, NVec3 extent, out NavRaycastHit hit)
        {
            NVec3 point = default, normal = default;
            int blocked = 0;
            bool success = NativeFunctionRegistry.Functions.NavRaycast(navMeshId, &start, &end, &extent, &point, &normal, &blocked) != 0;
            hit = new(blocked != 0, point, normal);
            return success;
        }
    }
    public unsafe sealed class NavMeshAgent
    {
        public uint Entity { get; }
        public NavMeshAgent(uint entity) { Entity = entity; }
        public NavPath FindPath(NVec3 destination, NVec3 extent)
        {
            var points = new NVec3[Navigation.MaxPoints];
            int count = 0;
            NavPathStatus status;
            fixed (NVec3* buffer = points)
                status = (NavPathStatus)NativeFunctionRegistry.Functions.NavAgentFindPath(Entity, &destination, &extent, buffer, points.Length, &count);
            Array.Resize(ref points, count);
            return new(status, points);
        }
    }
}
