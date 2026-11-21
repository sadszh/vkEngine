using System;
using System.Collections.Generic;
using System.Reflection;

namespace vkEngine.EngineCore
{
    public enum ComponentType
    {
        Transform = 0,
        Camera = 1,
        RenderableObject = 2,
        SkeletonAnimator = 3,
        RigidBody = 4,
        Sensor = 5,
        CharacterController = 6,
        DirectionalLight = 7,
        PointLight = 8,
        SpotLight = 9,
        Script = 10,
        AudioSource = 12,
        AudioListener = 13,
        Canvas = 14,
        NavMeshRegion = 15,
        NavMeshTile = 16,
        NavMeshAgent = 17
    }

    public interface IScriptLifecycle
    {
        void Start();

        void Update();

        void FixedUpdate();

        void LateUpdate();

        void Unload();
    }

    // Owned snapshot of exported fields, using the generated binary schema.
    public sealed record EntityScriptData(string ClassName, byte[] Data);

    internal enum ScriptState
    {
        NotStarted,
        Starting,
        Started,
        Unloading,
        Unloaded
    }

    public abstract unsafe class EntityScript : IScriptLifecycle
    {
        private readonly ScriptLifecycleMask lifecycleMask;
        public ScriptLifecycleMask LifecycleMask { get { return lifecycleMask; } }
        internal ScriptState State { get; private set; } = ScriptState.NotStarted;

        internal bool TryBeginStart()
        {
            if (State != ScriptState.NotStarted)
                return false;
            State = ScriptState.Starting;
            return true;
        }

        internal void MarkStarted()
        {
            // Start may unload or reset the script through a reentrant callback.
            if (State == ScriptState.Starting)
                State = ScriptState.Started;
        }

        internal void MarkUnloaded() { State = ScriptState.Unloaded; }

        internal bool TryBeginUnload()
        {
            if (State is ScriptState.Unloading or ScriptState.Unloaded)
                return false;
            State = ScriptState.Unloading;
            return true;
        }
        private static unsafe delegate* unmanaged[Cdecl]<UInt32, Int32, Int32> hasComponent;

        private static readonly Dictionary<Type, ScriptLifecycleMask> overrideMaskCache = new();
        private static readonly Dictionary<Type, ComponentType> componentTypeMap = new()
        {
            [typeof(Transform)] = ComponentType.Transform,
            [typeof(SkeletonAnimator)] = ComponentType.SkeletonAnimator,
            [typeof(RigidBody)] = ComponentType.RigidBody,
            [typeof(Sensor)] = ComponentType.Sensor,
            [typeof(NavMeshAgent)] = ComponentType.NavMeshAgent,
            [typeof(CharacterController)] = ComponentType.CharacterController,
            [typeof(DirectionalLight)] = ComponentType.DirectionalLight,
            [typeof(PointLight)] = ComponentType.PointLight,
            [typeof(SpotLight)] = ComponentType.SpotLight,
            [typeof(Canvas)] = ComponentType.Canvas,
            [typeof(AudioSource)] = ComponentType.AudioSource,
            [typeof(AudioListener)] = ComponentType.AudioListener

        };
        private static readonly Dictionary<ComponentType, Type> componentRuntimeTypeMap = new()
        {
            [ComponentType.Transform] = typeof(Transform),
            [ComponentType.SkeletonAnimator] = typeof(SkeletonAnimator),
            [ComponentType.RigidBody] = typeof(RigidBody),
            [ComponentType.Sensor] = typeof(Sensor),
            [ComponentType.NavMeshAgent] = typeof(NavMeshAgent),
            [ComponentType.CharacterController] = typeof(CharacterController),
            [ComponentType.DirectionalLight] = typeof(DirectionalLight),
            [ComponentType.PointLight] = typeof(PointLight),
            [ComponentType.SpotLight] = typeof(SpotLight),
            [ComponentType.Canvas] = typeof(Canvas),
            [ComponentType.AudioSource] = typeof(AudioSource),
            [ComponentType.AudioListener] = typeof(AudioListener)

        };

        protected EntityScript(UInt32 entity)
        {
            Entity = entity;
            lifecycleMask = GetOrAddLifecycleMask(GetType());
        }

        public UInt32 Entity { get; }
        public bool IsPendingDestroy => SceneManager.IsPendingDestroy(Entity);
        public void DestroyEntity() => SceneManager.DestroyEntity(Entity);

        internal static unsafe void RegisterNativeFunctions(NativeFunctions* functions)
        {
            hasComponent = functions->HasComponent;
        }

        public unsafe bool HasComponent(ComponentType componentType)
        {
            return hasComponent(Entity, (Int32)componentType) != 0;
        }

        public bool HasComponent<T>()
        {
            if (!componentTypeMap.TryGetValue(typeof(T), out ComponentType componentType))
                throw new NotSupportedException($"Component type '{typeof(T).FullName}' is not registered for native queries.");

            return HasComponent(componentType);
        }

        public T? GetComponent<T>() where T : class
        {
            return (T?)GetComponent(typeof(T));
        }

        public object? GetComponent(Type componentType)
        {
            if (componentType == null)
                throw new ArgumentNullException(nameof(componentType));

            if (!componentTypeMap.TryGetValue(componentType, out ComponentType nativeComponentType))
                throw new NotSupportedException($"Component type '{componentType.FullName}' is not registered for native queries.");

            if (!HasComponent(nativeComponentType))
                return null;

            return CreateComponent(componentType);
        }

        public object? GetComponent(ComponentType componentType)
        {
            if (!componentRuntimeTypeMap.TryGetValue(componentType, out Type? runtimeType))
                throw new NotSupportedException($"Component type '{componentType}' is not registered for managed access.");

            if (!HasComponent(componentType))
                return null;

            return CreateComponent(runtimeType);
        }

        public virtual void Start() { }

        public virtual void Update() { }

        public virtual void FixedUpdate() { }

        public virtual void LateUpdate() { }

        public virtual void Unload() { }

        public EntityScriptData FillData()
        {
            if (State == ScriptState.Unloaded)
                throw new InvalidOperationException("Cannot export an unloaded script.");
            return SceneManager.FillData(this);
        }

        private static ScriptLifecycleMask GetOrAddLifecycleMask(Type type)
        {
            if (overrideMaskCache.TryGetValue(type, out var mask))
            {
                return mask;
            }

            mask = ScriptLifecycleMask.None;
            if (IsMethodOverridden(type, nameof(Start)))
                mask |= ScriptLifecycleMask.Start;
            if (IsMethodOverridden(type, nameof(Update)))
                mask |= ScriptLifecycleMask.Update;
            if (IsMethodOverridden(type, nameof(FixedUpdate)))
                mask |= ScriptLifecycleMask.FixedUpdate;
            if (IsMethodOverridden(type, nameof(LateUpdate)))
                mask |= ScriptLifecycleMask.LateUpdate;
            if (IsMethodOverridden(type, nameof(Unload)))
                mask |= ScriptLifecycleMask.Unload;

            overrideMaskCache[type] = mask;
            return mask;
        }

        private static bool IsMethodOverridden(Type type, string methodName)
        {
            var method = type.GetMethod(methodName, BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic);
            if (method == null)
                return false;
            return method.GetBaseDefinition().DeclaringType != method.DeclaringType;
        }

        private object CreateComponent(Type componentType)
        {
            ConstructorInfo? ctor = componentType.GetConstructor(new[] { typeof(UInt32) });
            if (ctor == null)
                throw new NotSupportedException($"Component type '{componentType.FullName}' must define a public constructor with a UInt32 entity parameter.");

            return ctor.Invoke(new object[] { Entity });
        }
    }
}
