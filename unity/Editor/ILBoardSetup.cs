#if UNITY_EDITOR
// Immersive Locomotion — OneWheel/skateboard board avatar setup.
//
// Generates the animator, clips and synced parameters that the overlay's OSC
// output drives. Merge the generated controller with VRCFury (Full Controller)
// so nothing on your base avatar is overwritten. Constraints (board -> feet)
// are set up by you separately.
//
// Overlay -> VRChat OSC parameters (sent by immersive_locomotion.exe):
//   IL_BoardActive : Bool   — true while the board is active (you're riding)
//   IL_BoardSpeed  : Float  — -1..1, signed wheel speed (normalized)
//
// Menu: Tools ▸ Immersive Locomotion ▸ Setup Board

using System.IO;
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
    Vector3 spinAxis = new Vector3( 1, 0, 0 ); // wheel local spin axis
    float wheelDegPerSec = 720f; // spin rate at full IL_BoardSpeed

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
                + "transform (spins). Generate, then add a VRCFury 'Full "
                + "Controller' pointing at the generated controller. "
                + "Constraints are handled separately.",
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

        EditorUtility.DisplayDialog(
            "Immersive Locomotion",
            "Generated in " + kOutDir + ":\n"
                + "  IL_Board.controller\n"
                + "  IL_Board_Params.asset  (IL_BoardActive, IL_BoardSpeed, "
                + "both synced)\n"
                + ( motorClip != null
                        ? "  Motor audio: AudioSource added; volume + pitch "
                          + "follow IL_BoardSpeed.\n"
                        : "" )
                + "\n"
                + "In your VRCFury Full Controller:\n"
                + "  - Controller = IL_Board.controller\n"
                + "  - Parameters = IL_Board_Params.asset\n"
                + "  - Global Parameters += 'IL_*'  (REQUIRED)\n\n"
                + "Without the global entry VRCFury prefixes the params and "
                + "the OSC names (/avatar/parameters/IL_BoardActive, "
                + "IL_BoardSpeed) won't match.",
            "OK" );

        Selection.activeObject = prms;
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

    // One linear revolution about the wheel's dominant local axis. Only that
    // one euler axis is animated (the other two keep the wheel's authored
    // orientation), the curve is truly linear (constant angular velocity), and
    // the clip is exactly one revolution long so there is no frozen tail.
    AnimationClip SpinClip( string path )
    {
        var clip = new AnimationClip();
        float dur = 360f / Mathf.Max( 1f, wheelDegPerSec );

        Vector3 s = spinAxis.normalized;
        float ax = Mathf.Abs( s.x ), ay = Mathf.Abs( s.y ), az = Mathf.Abs( s.z );
        string prop;
        float sign;
        if ( ax >= ay && ax >= az )
        {
            prop = "localEulerAnglesRaw.x";
            sign = s.x < 0 ? -1f : 1f;
        }
        else if ( ay >= az )
        {
            prop = "localEulerAnglesRaw.y";
            sign = s.y < 0 ? -1f : 1f;
        }
        else
        {
            prop = "localEulerAnglesRaw.z";
            sign = s.z < 0 ? -1f : 1f;
        }

        var curve = AnimationCurve.Linear( 0f, 0f, dur, 360f * sign );
        clip.SetCurve( path, typeof( Transform ), prop, curve );

        var settings = AnimationUtility.GetAnimationClipSettings( clip );
        settings.loopTime = true;
        AnimationUtility.SetAnimationClipSettings( clip, settings );
        AssetDatabase.CreateAsset( clip, kOutDir + "/IL_WheelSpin.anim" );
        return clip;
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
