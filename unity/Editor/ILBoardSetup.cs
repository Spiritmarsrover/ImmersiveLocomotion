#if UNITY_EDITOR
// Immersive Locomotion — OneWheel/skateboard board avatar setup.
//
// Generates the animator, clips and synced parameters that the overlay's OSC
// output drives, and (optionally) auto-wires a VRCFury Full Controller and the
// board -> feet VRC constraints so the board is drag-and-drop.
//
// Overlay -> VRChat OSC parameters (sent by immersive_locomotion.exe):
//   IL_BoardActive : Bool   — true while the board is active (you're riding)
//   IL_BoardSpeed  : Float  — -1..1, signed wheel speed (normalized)
//
// Menu: Tools ▸ Immersive Locomotion ▸ Setup Board

using System.IO;
using System.Reflection;
using UnityEditor;
using UnityEditor.Animations;
using UnityEngine;
using VRC.SDK3.Avatars.Components;
using VRC.SDK3.Avatars.ScriptableObjects;

public class ILBoardSetup : EditorWindow
{
    VRCAvatarDescriptor avatar;
    GameObject boardRoot; // parent of the board mesh; toggled on/off
    Transform wheel;      // the wheel mesh transform that spins
    Vector3 spinAxis = new Vector3( 0, 0, 1 ); // wheel local spin axis (Z)
    float wheelDegPerSec = 720f; // spin rate at full IL_BoardSpeed
    bool autoVrcfury = true; // auto-add a VRCFury Full Controller (drag-drop)

    // board -> feet: two anchor empties (one per foot bone) drive a VRC
    // position constraint (board sits at the feet midpoint) + an aim
    // constraint (board aligns along the stance line)
    bool setupFeetConstraints = true;
    // local offset of each anchor on its foot bone; x is mirrored per side
    Vector3 footAnchorOffset = new Vector3( 0.009f, 0.148f, -0.012f );
    Vector3 boardAimAxis = new Vector3( -1, 0, 0 ); // board axis that aims down
                                                    // the stance line

    // optional motor sound: one-pitch loop whose volume + pitch scale with
    // |IL_BoardSpeed|
    AudioClip motorClip;
    GameObject audioObject; // where the AudioSource goes (default: board root)
    float motorMaxVolume = 1.0f;
    float motorVolFullAt = 0.2f; // |speed| where volume reaches max (fast rise)
    float motorMinPitch = 0.8f;  // pitch at 0 speed
    float motorMaxPitch = 2.0f;  // pitch at full speed
    float motorVolumetricRadius = 2.0f; // must EXCEED board->head distance
                                        // (~1.5m) so your head is inside the
                                        // sphere -> non-directional -> smooth

    const string kOutDir = "Assets/Wheeler/Generated";
    const string kActiveParam = "IL_BoardActive";
    const string kSpeedParam = "IL_BoardSpeed";

    [MenuItem( "Tools/Immersive Locomotion/Setup Board" )]
    static void Open() => GetWindow<ILBoardSetup>( "IL Board Setup" );

    void OnGUI()
    {
        EditorGUILayout.HelpBox(
            "Assign your avatar, the board root (toggled) and the wheel "
                + "transform (spins), then Generate. With the options below on, "
                + "a VRCFury Full Controller and the board->feet VRC "
                + "constraints are set up for you (drag-and-drop, nothing to "
                + "configure).",
            MessageType.Info );

        avatar = (VRCAvatarDescriptor)EditorGUILayout.ObjectField(
            "Avatar", avatar, typeof( VRCAvatarDescriptor ), true );
        boardRoot = (GameObject)EditorGUILayout.ObjectField(
            "Board Root", boardRoot, typeof( GameObject ), true );
        wheel = (Transform)EditorGUILayout.ObjectField(
            "Wheel", wheel, typeof( Transform ), true );

        EditorGUILayout.Space();
        spinAxis = EditorGUILayout.Vector3Field( "Wheel Spin Axis (local)",
                                                 spinAxis );
        wheelDegPerSec = EditorGUILayout.FloatField(
            "Wheel deg/s at full speed", wheelDegPerSec );

        EditorGUILayout.Space();
        EditorGUILayout.LabelField( "Motor sound (optional)",
                                    EditorStyles.boldLabel );
        motorClip = (AudioClip)EditorGUILayout.ObjectField(
            "Motor Loop", motorClip, typeof( AudioClip ), false );
        using ( new EditorGUI.DisabledScope( motorClip == null ) )
        {
            audioObject = (GameObject)EditorGUILayout.ObjectField(
                "Audio Object (default board root)", audioObject,
                typeof( GameObject ), true );
            motorMaxVolume = EditorGUILayout.Slider( "Max Volume",
                                                     motorMaxVolume, 0f, 1f );
            motorVolFullAt = EditorGUILayout.Slider(
                "Volume full at |speed|", motorVolFullAt, 0.05f, 1f );
            motorMinPitch = EditorGUILayout.FloatField( "Pitch @ 0 speed",
                                                        motorMinPitch );
            motorMaxPitch = EditorGUILayout.FloatField( "Pitch @ full speed",
                                                        motorMaxPitch );
            motorVolumetricRadius = EditorGUILayout.Slider(
                "Spatial volumetric radius (m)", motorVolumetricRadius, 0f,
                5f );
        }

        EditorGUILayout.Space();
        autoVrcfury = EditorGUILayout.ToggleLeft(
            "Auto-add VRCFury Full Controller (recommended)", autoVrcfury );

        setupFeetConstraints = EditorGUILayout.ToggleLeft(
            "Set up board->feet VRC constraints", setupFeetConstraints );
        using ( new EditorGUI.DisabledScope( !setupFeetConstraints ) )
        {
            footAnchorOffset = EditorGUILayout.Vector3Field(
                "Foot anchor offset (x mirrored)", footAnchorOffset );
            boardAimAxis = EditorGUILayout.Vector3Field( "Board aim axis",
                                                         boardAimAxis );
        }

        using ( new EditorGUI.DisabledScope(
            avatar == null || boardRoot == null || wheel == null ) )
        {
            if ( GUILayout.Button( "Generate" ) )
                Generate();
        }
    }

    void Generate()
    {
        Directory.CreateDirectory( kOutDir );

        string boardPath = RelPath( avatar.transform, boardRoot.transform );
        string wheelPath = RelPath( avatar.transform, wheel );

        // --- clips ---
        AnimationClip boardOff = ToggleClip( boardPath, false, "IL_BoardOff" );
        AnimationClip boardOn = ToggleClip( boardPath, true, "IL_BoardOn" );
        AnimationClip spin = SpinClip( wheelPath );

        // --- controller ---
        var ctrl = AnimatorController.CreateAnimatorControllerAtPath(
            kOutDir + "/IL_Board.controller" );
        ctrl.AddParameter( kActiveParam, AnimatorControllerParameterType.Bool );
        ctrl.AddParameter( kSpeedParam, AnimatorControllerParameterType.Float );

        var actLayer = NewLayer( ctrl, "IL_BoardActive" );
        var sm = actLayer.stateMachine;
        var off = sm.AddState( "Off", new Vector3( 300, 0, 0 ) );
        off.motion = boardOff;
        off.writeDefaultValues = false;
        var on = sm.AddState( "On", new Vector3( 300, 120, 0 ) );
        on.motion = boardOn;
        on.writeDefaultValues = false;
        sm.defaultState = off;
        var toOn = off.AddTransition( on );
        toOn.hasExitTime = false;
        toOn.duration = 0;
        toOn.AddCondition( AnimatorConditionMode.If, 0, kActiveParam );
        var toOff = on.AddTransition( off );
        toOff.hasExitTime = false;
        toOff.duration = 0;
        toOff.AddCondition( AnimatorConditionMode.IfNot, 0, kActiveParam );

        var wheelLayer = NewLayer( ctrl, "IL_Wheel" );
        var spinState = wheelLayer.stateMachine.AddState(
            "Spin", new Vector3( 300, 0, 0 ) );
        spinState.motion = spin;
        spinState.writeDefaultValues = false;
        spinState.speedParameterActive = true;
        spinState.speedParameter = kSpeedParam;
        wheelLayer.stateMachine.defaultState = spinState;

        // --- optional motor audio (volume + pitch by IL_BoardSpeed) ---
        if ( motorClip != null )
            SetupMotorAudio( ctrl );

        // --- synced expression parameters asset (plug into Full Controller) ---
        var prms = GenerateParams();

        AssetDatabase.SaveAssets();

        // --- drag-and-drop: auto-wire the VRCFury Full Controller ---
        bool wired = false;
        GameObject holder = null;
        if ( autoVrcfury )
        {
            holder = MakeVrcfuryHolder();
            wired = TryWireVrcfury( holder, ctrl, prms );
            if ( !wired && holder != null )
            {
                Undo.DestroyObjectImmediate( holder );
                holder = null;
            }
        }

        // --- board -> feet VRC constraints ---
        bool constrained = false;
        if ( setupFeetConstraints )
            constrained = TrySetupFeetConstraints();

        string msg = "Generated in " + kOutDir + ":\n"
            + "  IL_Board.controller\n"
            + "  IL_Board_Params.asset  (IL_BoardActive, IL_BoardSpeed, both "
            + "synced)\n"
            + ( motorClip != null
                    ? "  Motor audio: AudioSource added; volume + pitch follow "
                      + "IL_BoardSpeed.\n"
                    : "" );
        if ( wired )
            msg += "\nVRCFury Full Controller auto-added on '" + holder.name
                 + "' (controller + params merged, IL_* kept global). Nothing "
                 + "else to wire - just set up your constraints and upload.";
        else
            msg += "\n"
                 + ( autoVrcfury ? "VRCFury not found - add a Full Controller "
                                   + "yourself:\n"
                                 : "Add a VRCFury Full Controller yourself:\n" )
                 + "  - Controller = IL_Board.controller\n"
                 + "  - Parameters = IL_Board_Params.asset\n"
                 + "  - Global Parameters += 'IL_*'  (REQUIRED, or the OSC "
                 + "names won't match).";
        if ( setupFeetConstraints )
            msg += constrained
                ? "\nFeet: IL_FootAnchor_L/R added on the foot bones + VRC "
                  + "position/aim constraints on the board root."
                : "\nFeet constraints skipped (needs a humanoid avatar with "
                  + "foot bones and VRC Constraints in your SDK).";

        EditorUtility.DisplayDialog( "Immersive Locomotion", msg, "OK" );
        Selection.activeObject = wired ? (Object)holder : prms;
    }

    // Fresh child under the avatar to hold the VRCFury component; destroyed
    // and recreated each Generate so re-running never stacks duplicates.
    GameObject MakeVrcfuryHolder()
    {
        var prev = avatar.transform.Find( "IL_Board_VRCFury" );
        if ( prev != null )
            Undo.DestroyObjectImmediate( prev.gameObject );
        var go = new GameObject( "IL_Board_VRCFury" );
        Undo.RegisterCreatedObjectUndo( go, "IL VRCFury" );
        go.transform.SetParent( avatar.transform, false );
        return go;
    }

    // Wire a VRCFury Full Controller entirely through its public API by
    // reflection, so this script compiles with or without VRCFury installed
    // (returns false -> caller falls back to manual instructions). The base
    // object is overridden to the avatar root so the avatar-relative clip
    // paths resolve no matter where the holder sits.
    bool TryWireVrcfury( GameObject holder, RuntimeAnimatorController ctrl,
                         VRCExpressionParameters prms )
    {
        System.Type fc = FindType( "com.vrcfury.api.FuryComponents" );
        var create = fc?.GetMethod(
            "CreateFullController", BindingFlags.Public | BindingFlags.Static,
            null, new[] { typeof( GameObject ) }, null );
        if ( create == null )
            return false;

        object fury = create.Invoke( null, new object[] { holder } );
        if ( fury == null )
            return false;
        System.Type ft = fury.GetType();

        ft.GetMethod( "AddController",
                      new[] { typeof( RuntimeAnimatorController ),
                              typeof( VRCAvatarDescriptor.AnimLayerType ) } )
            ?.Invoke( fury, new object[] {
                          ctrl, VRCAvatarDescriptor.AnimLayerType.FX } );
        ft.GetMethod( "AddParams", new[] { typeof( VRCExpressionParameters ) } )
            ?.Invoke( fury, new object[] { prms } );
        var addGlobal = ft.GetMethod( "AddGlobalParam",
                                      new[] { typeof( string ) } );
        addGlobal?.Invoke( fury, new object[] { kActiveParam } );
        addGlobal?.Invoke( fury, new object[] { kSpeedParam } );

        // point the Full Controller's base object at the avatar root
        var model = ft.GetField( "c",
                                 BindingFlags.NonPublic | BindingFlags.Instance )
                        ?.GetValue( fury );
        model?.GetType().GetField( "rootObjOverride" )
            ?.SetValue( model, avatar.gameObject );
        return true;
    }

    static System.Type FindType( string fullName )
    {
        foreach ( var a in System.AppDomain.CurrentDomain.GetAssemblies() )
        {
            var t = a.GetType( fullName );
            if ( t != null )
                return t;
        }
        return null;
    }

    // Replicate the board->feet rig by reflection (so this compiles without
    // the VRC Constraints package): an anchor empty on each foot bone, a
    // VRCPositionConstraint on the board root averaging both anchors (board
    // sits at the feet midpoint), and a VRCAimConstraint toward one anchor
    // (board's aim axis aligns down the stance line). Returns false -> caller
    // reports it was skipped.
    bool TrySetupFeetConstraints()
    {
        System.Type posT = FindType(
            "VRC.SDK3.Dynamics.Constraint.Components.VRCPositionConstraint" );
        System.Type aimT = FindType(
            "VRC.SDK3.Dynamics.Constraint.Components.VRCAimConstraint" );
        System.Type srcT = FindType( "VRC.Dynamics.VRCConstraintSource" );
        if ( posT == null || aimT == null || srcT == null )
            return false;

        var anim = avatar.GetComponent<Animator>();
        if ( anim == null || !anim.isHuman )
            return false;
        var lFoot = anim.GetBoneTransform( HumanBodyBones.LeftFoot );
        var rFoot = anim.GetBoneTransform( HumanBodyBones.RightFoot );
        if ( lFoot == null || rFoot == null )
            return false;

        Transform lAnchor = MakeFootAnchor(
            lFoot, "IL_FootAnchor_L",
            new Vector3( -footAnchorOffset.x, footAnchorOffset.y,
                         footAnchorOffset.z ) );
        Transform rAnchor = MakeFootAnchor(
            rFoot, "IL_FootAnchor_R",
            new Vector3( footAnchorOffset.x, footAnchorOffset.y,
                         footAnchorOffset.z ) );

        // remove any board->feet constraints we added on a previous run
        foreach ( var c in boardRoot.GetComponents<Component>() )
            if ( c != null && ( c.GetType() == posT || c.GetType() == aimT ) )
                Undo.DestroyObjectImmediate( c );

        var pos = Undo.AddComponent( boardRoot, posT );
        SetMember( pos, "IsActive", true );
        SetMember( pos, "AffectsPositionX", true );
        SetMember( pos, "AffectsPositionY", true );
        SetMember( pos, "AffectsPositionZ", true );
        AddConstraintSource( pos, srcT, lAnchor, 1f );
        AddConstraintSource( pos, srcT, rAnchor, 1f );

        var aim = Undo.AddComponent( boardRoot, aimT );
        SetMember( aim, "IsActive", true );
        SetMember( aim, "AffectsRotationX", true );
        SetMember( aim, "AffectsRotationY", true );
        SetMember( aim, "AffectsRotationZ", true );
        SetMember( aim, "AimAxis", boardAimAxis );
        SetMember( aim, "UpAxis", new Vector3( 0, 1, 0 ) );
        AddConstraintSource( aim, srcT, rAnchor, 1f );
        return true;
    }

    Transform MakeFootAnchor( Transform foot, string name, Vector3 localOffset )
    {
        var prev = foot.Find( name );
        if ( prev != null )
            Undo.DestroyObjectImmediate( prev.gameObject );
        var go = new GameObject( name );
        Undo.RegisterCreatedObjectUndo( go, "IL foot anchor" );
        go.transform.SetParent( foot, false );
        go.transform.localPosition = localOffset;
        go.transform.localRotation = Quaternion.identity;
        return go.transform;
    }

    static void AddConstraintSource( object constraint, System.Type srcT,
                                     Transform src, float weight )
    {
        object sources = GetMember( constraint, "Sources" );
        var ctor = srcT.GetConstructor( new[] {
            typeof( Transform ), typeof( float ), typeof( Vector3 ),
            typeof( Vector3 ) } );
        if ( sources == null || ctor == null )
            return;
        object entry = ctor.Invoke(
            new object[] { src, weight, Vector3.zero, Vector3.zero } );
        sources.GetType().GetMethod( "Add", new[] { srcT } )
            ?.Invoke( sources, new[] { entry } );
    }

    static void SetMember( object o, string name, object value )
    {
        var f = o.GetType().GetField( name );
        if ( f != null ) { f.SetValue( o, value ); return; }
        var p = o.GetType().GetProperty( name );
        if ( p != null && p.CanWrite )
            p.SetValue( o, value );
    }

    static object GetMember( object o, string name )
    {
        var f = o.GetType().GetField( name );
        if ( f != null )
            return f.GetValue( o );
        return o.GetType().GetProperty( name )?.GetValue( o );
    }

    AnimationClip ToggleClip( string path, bool active, string name )
    {
        var clip = new AnimationClip();
        var curve = new AnimationCurve(
            new Keyframe( 0f, active ? 1f : 0f ),
            new Keyframe( 1f / 60f, active ? 1f : 0f ) );
        clip.SetCurve( path, typeof( GameObject ), "m_IsActive", curve );
        AssetDatabase.CreateAsset( clip, kOutDir + "/" + name + ".anim" );
        return clip;
    }

    // One linear revolution about the wheel's dominant local axis, starting
    // from the wheel's *authored* local rotation so the spin neither snaps the
    // wheel to zero nor loses any authored tilt. All three euler axes are
    // written (the spin axis advances 360 deg, the other two are held at their
    // authored angle), which fully defines the rotation for Write-Defaults-off.
    // The curve is truly linear (constant angular velocity) and exactly one
    // revolution long, so there is no frozen tail.
    AnimationClip SpinClip( string path )
    {
        var clip = new AnimationClip();
        float dur = 360f / Mathf.Max( 1f, wheelDegPerSec );

        Vector3 e0 = wheel.localEulerAngles; // authored orientation

        Vector3 s = spinAxis.normalized;
        float ax = Mathf.Abs( s.x ), ay = Mathf.Abs( s.y ), az = Mathf.Abs( s.z );
        int axis;
        float sign;
        if ( ax >= ay && ax >= az ) { axis = 0; sign = s.x < 0 ? -1f : 1f; }
        else if ( ay >= az ) { axis = 1; sign = s.y < 0 ? -1f : 1f; }
        else { axis = 2; sign = s.z < 0 ? -1f : 1f; }

        SpinAxisCurve( clip, path, "localEulerAnglesRaw.x", e0.x, axis == 0,
                       sign, dur );
        SpinAxisCurve( clip, path, "localEulerAnglesRaw.y", e0.y, axis == 1,
                       sign, dur );
        SpinAxisCurve( clip, path, "localEulerAnglesRaw.z", e0.z, axis == 2,
                       sign, dur );

        var settings = AnimationUtility.GetAnimationClipSettings( clip );
        settings.loopTime = true;
        AnimationUtility.SetAnimationClipSettings( clip, settings );
        AssetDatabase.CreateAsset( clip, kOutDir + "/IL_WheelSpin.anim" );
        return clip;
    }

    // Spin axis: one linear revolution from `start`. Other axes: constant at
    // `start` (a flat curve over the same duration).
    static void SpinAxisCurve( AnimationClip clip, string path, string prop,
                               float start, bool spinning, float sign,
                               float dur )
    {
        var curve = spinning
            ? AnimationCurve.Linear( 0f, start, dur, start + 360f * sign )
            : AnimationCurve.Linear( 0f, start, dur, start );
        clip.SetCurve( path, typeof( Transform ), prop, curve );
    }

    // Motor: an AudioSource whose volume + pitch are driven by a 1D blend
    // tree on IL_BoardSpeed. Thresholds at -1/0/+1 make it symmetric, so the
    // motor revs the same forward or backward (it follows wheel speed).
    void SetupMotorAudio( AnimatorController ctrl )
    {
        var obj = audioObject != null ? audioObject : boardRoot;
        var src = obj.GetComponent<AudioSource>();
        if ( src == null )
            src = obj.AddComponent<AudioSource>();
        src.clip = motorClip;
        src.loop = true;
        src.playOnAwake = true;
        src.spatialBlend = 1f; // 3D (VRChat forces this anyway)
        src.volume = 0f;
        src.pitch = motorMinPitch;
        // The board is constrained to the feet and moves fast; with Doppler on
        // the engine constantly pitch-shifts by relative velocity -> crackle.
        src.dopplerLevel = 0f;

        // Close-range 3D avatar audio crackles when the source is a pinpoint
        // (Volumetric Radius 0) because the HRTF/pan updates hard as the head
        // moves. A non-zero radius makes it a sphere -> smooth up close.
        var sp = obj.GetComponent<VRC.SDKBase.VRC_SpatialAudioSource>();
        if ( sp == null )
            sp = obj.AddComponent<VRC.SDKBase.VRC_SpatialAudioSource>();
        sp.EnableSpatialization = true;
        sp.VolumetricRadius = motorVolumetricRadius;

        // Pitch-shifting a compressed clip re-decodes on the fly and crackles.
        // Force fully-decompressed PCM so the resampler has clean samples.
        string clipPath = AssetDatabase.GetAssetPath( motorClip );
        if ( AssetImporter.GetAtPath( clipPath ) is AudioImporter imp )
        {
            var ss = imp.defaultSampleSettings;
            ss.loadType = AudioClipLoadType.DecompressOnLoad;
            ss.compressionFormat = AudioCompressionFormat.PCM;
            imp.defaultSampleSettings = ss;
            imp.SaveAndReimport();
        }

        // Volume rises 0 -> max over |speed| 0..r (fast), then holds; pitch
        // rises minPitch -> maxPitch across the whole range. Different curves,
        // so we need an intermediate threshold at r where volume is already
        // max but pitch is only partway.
        float r = Mathf.Clamp( motorVolFullAt, 0.05f, 0.95f );
        float pitchAtR = Mathf.Lerp( motorMinPitch, motorMaxPitch, r );

        string path = RelPath( avatar.transform, obj.transform );
        var center = AudioClipState( path, 0f, motorMinPitch, "IL_Motor0" );
        var rise = AudioClipState( path, motorMaxVolume, pitchAtR,
                                   "IL_MotorRise" );
        var loud = AudioClipState( path, motorMaxVolume, motorMaxPitch,
                                   "IL_MotorLoud" );

        var bt = new BlendTree
        {
            name = "IL_MotorBlend",
            blendType = BlendTreeType.Simple1D,
            blendParameter = kSpeedParam,
            useAutomaticThresholds = false,
        };
        bt.children = new[]
        {
            new ChildMotion { motion = loud, threshold = -1f, timeScale = 1f },
            new ChildMotion { motion = rise, threshold = -r, timeScale = 1f },
            new ChildMotion { motion = center, threshold = 0f, timeScale = 1f },
            new ChildMotion { motion = rise, threshold = r, timeScale = 1f },
            new ChildMotion { motion = loud, threshold = 1f, timeScale = 1f },
        };
        AssetDatabase.AddObjectToAsset( bt, ctrl );

        var layer = NewLayer( ctrl, "IL_Audio" );
        var st = layer.stateMachine.AddState( "Motor",
                                              new Vector3( 300, 0, 0 ) );
        st.motion = bt;
        st.writeDefaultValues = false;
        layer.stateMachine.defaultState = st;
    }

    AnimationClip AudioClipState( string path, float vol, float pitch,
                                  string name )
    {
        var clip = new AnimationClip();
        clip.SetCurve( path, typeof( AudioSource ), "m_Volume",
                       AnimationCurve.Linear( 0f, vol, 1f / 60f, vol ) );
        clip.SetCurve( path, typeof( AudioSource ), "m_Pitch",
                       AnimationCurve.Linear( 0f, pitch, 1f / 60f, pitch ) );
        AssetDatabase.CreateAsset( clip, kOutDir + "/" + name + ".anim" );
        return clip;
    }

    static AnimatorControllerLayer NewLayer( AnimatorController c,
                                             string name )
    {
        c.AddLayer( name );
        var layers = c.layers;
        layers[layers.Length - 1].defaultWeight = 1f;
        c.layers = layers;
        return c.layers[c.layers.Length - 1];
    }

    // Standalone synced-parameters asset for the VRCFury Full Controller to
    // merge — keeps this prop self-contained instead of editing the avatar's
    // own expression parameters.
    VRCExpressionParameters GenerateParams()
    {
        var ep = ScriptableObject.CreateInstance<VRCExpressionParameters>();
        ep.parameters = new[]
        {
            new VRCExpressionParameters.Parameter
            {
                name = kActiveParam,
                valueType = VRCExpressionParameters.ValueType.Bool,
                defaultValue = 0,
                saved = false,
                networkSynced = true,
            },
            new VRCExpressionParameters.Parameter
            {
                name = kSpeedParam,
                valueType = VRCExpressionParameters.ValueType.Float,
                defaultValue = 0,
                saved = false,
                networkSynced = true,
            },
        };
        AssetDatabase.CreateAsset( ep, kOutDir + "/IL_Board_Params.asset" );
        return ep;
    }

    static string RelPath( Transform root, Transform t )
    {
        string path = t.name;
        for ( var p = t.parent; p != null && p != root; p = p.parent )
            path = p.name + "/" + path;
        return path;
    }
}
#endif
