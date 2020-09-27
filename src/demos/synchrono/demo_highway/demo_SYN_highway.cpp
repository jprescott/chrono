#include "chrono_synchrono/cli/SynCLI.h"
#include "chrono_synchrono/communication/mpi/SynMPIManager.h"

#include "chrono_synchrono/framework/SynFramework.h"
#include "chrono_synchrono/utils/SynDataLoader.h"

#include "chrono_synchrono/visualization/SynVisualizationManager.h"

#include "chrono_synchrono/brain/SynVehicleBrain.h"
#include "chrono_synchrono/brain/SynACCBrain.h"
#include "chrono_synchrono/agent/SynWheeledVehicleAgent.h"
#include "chrono_synchrono/agent/SynEnvironmentAgent.h"
#include "chrono_synchrono/vehicle/SynWheeledVehicle.h"

#include "chrono_synchrono/terrain/SynRigidTerrain.h"

#ifdef CHRONO_IRRLICHT
#include "chrono_synchrono/visualization/SynIrrVehicleVisualization.h"
#endif

#ifdef CHRONO_SENSOR
#include "chrono_synchrono/visualization/SynSensorVisualization.h"
#endif

#include <chrono>

using namespace chrono;
using namespace chrono::geometry;
using namespace chrono::synchrono;

// ------------------------------------------------------------------------------------

std::shared_ptr<SynWheeledVehicle> InitializeVehicle(int rank) {
    ChVector<> init_loc;
    ChQuaternion<> init_rot;
    std::string filename;
    switch (rank) {
        case 0:
            filename = "vehicle/Sedan.json";
            init_rot = Q_from_AngZ(90 * CH_C_DEG_TO_RAD);
            init_loc = ChVector<>(2.8, -70, 0.2);
            break;
        case 1:
            filename = "vehicle/Sedan.json";
            init_rot = Q_from_AngZ(90 * CH_C_DEG_TO_RAD);
            init_loc = ChVector<>(2.8, -40, 0.2);
            break;
        case 2:

            filename = "vehicle/CityBus.json";
            init_rot = Q_from_AngZ(90 * CH_C_DEG_TO_RAD);
            init_loc = ChVector<>(6.4, 0, 0.2);
            break;
        default:
            if (rank % 2 == 0) {
                filename = "vehicle/Sedan.json";
                init_loc = ChVector<>(-2.8, 70.0 - (rank - 4.0) * 30, 0.2);
            } else {
                filename = "vehicle/CityBus.json";
                init_loc = ChVector<>(-6.4, 70.0 - (rank - 4.0) * 30, 0.2);
            }
            init_rot = Q_from_AngZ(-90 * CH_C_DEG_TO_RAD);
    }
    auto vehicle = chrono_types::make_shared<SynWheeledVehicle>(GetSynDataFile(filename), CONTACT_METHOD);
    vehicle->Initialize(ChCoordsys<>(init_loc, init_rot));

    return vehicle;
}

// ------------------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    // Initialize the MPIManager
    SynMPIManager mpi_manager(argc, argv, MPI_CONFIG_DEFAULT);
    int rank = mpi_manager.GetRank();
    int num_ranks = mpi_manager.GetNumRanks();

    // CLI tools for default synchrono demos
    SynCLI cli(argv[0]);
    cli.AddDefaultDemoOptions();
    if (!cli.Parse(argc, argv, rank == 0))
        mpi_manager.Exit();

    std::shared_ptr<ChDriver> driver;

    // -------
    // Vehicle
    // -------
    auto agent = chrono_types::make_shared<SynWheeledVehicleAgent>(rank);
    agent->SetVehicle(InitializeVehicle(rank));
    mpi_manager.AddAgent(agent, rank);

    // -------
    // Terrain
    // -------
    auto terrain = chrono_types::make_shared<RigidTerrain>(agent->GetSystem());

    auto patch = terrain->AddPatch(DefaultMaterialSurface(), CSYSNORM, GetSynDataFile("meshes/Highway_col.obj"), "",
                                   0.01, false);

    auto vis_mesh = chrono_types::make_shared<ChTriangleMeshConnected>();
    vis_mesh->LoadWavefrontMesh(GetSynDataFile("meshes/Highway_vis.obj"), true, true);

    auto trimesh_shape = chrono_types::make_shared<ChTriangleMeshShape>();
    trimesh_shape->SetMesh(vis_mesh);
    trimesh_shape->SetStatic(true);

    patch->GetGroundBody()->AddAsset(trimesh_shape);

    terrain->Initialize();
    agent->SetTerrain(chrono_types::make_shared<SynRigidTerrain>(terrain));

    // ----------
    // Controller
    // ----------
    auto loc = agent->GetChVehicle().GetVehiclePos();
    auto curve_pts = rank < 4 ? std::vector<ChVector<>>({loc, loc + ChVector<>(0, 140, 0)})   //
                              : std::vector<ChVector<>>({loc, loc - ChVector<>(0, 140, 0)});  //
    auto path = chrono_types::make_shared<ChBezierCurve>(curve_pts);

    double target_speed = rank == 2 ? 6 : 10;
    double target_following_time = 1.2;
    double target_min_distance = 10;
    double current_distance = 100;
    bool is_path_closed = false;

    if (rank != 0) {
        auto acc_driver = chrono_types::make_shared<ChPathFollowerACCDriver>(
            agent->GetChVehicle(), path, "Highway", target_speed, target_following_time, target_min_distance,
            current_distance, is_path_closed);
        acc_driver->GetSpeedController().SetGains(0.4, 0.0, 0.0);
        acc_driver->GetSteeringController().SetGains(0.4, 0.1, 0.2);
        acc_driver->GetSteeringController().SetLookAheadDistance(5);

        driver = acc_driver;
    } else {
        std::vector<ChVector<>> curve_pts2 = {ChVector<>({6.4, -70, 0.2}), ChVector<>(6.4, 70, 0.2)};
        auto path2 = chrono_types::make_shared<ChBezierCurve>(curve_pts2);

        std::vector<std::pair<std::shared_ptr<ChBezierCurve>, bool>> path_pairs;
        path_pairs.push_back({path, false});
        path_pairs.push_back({path2, false});

        auto acc_driver = chrono_types::make_shared<ChMulPathFollowerACCDriver>(
            agent->GetChVehicle(), path_pairs, "Highway", target_speed, target_following_time, target_min_distance,
            current_distance);

        acc_driver->GetSpeedController().SetGains(0.4, 0.0, 0.0);
        acc_driver->GetSteeringController().SetGains(0.4, 0.1, 0.2);
        acc_driver->GetSteeringController().SetLookAheadDistance(5);

        driver = acc_driver;
    }

    auto brain = chrono_types::make_shared<SynACCBrain>(rank, driver, agent->GetChVehicle());
    if (rank == 1)
        brain->setMultipath(true);
    agent->SetBrain(brain);

    auto vis_manager = chrono_types::make_shared<SynVisualizationManager>();
    agent->AttachVisualizationManager(vis_manager);

#ifdef CHRONO_IRRLICHT
    if (cli.HasValueInVector<int>("irr", rank)) {
        auto irr_vis = chrono_types::make_shared<SynIrrVehicleVisualization>(driver);
        irr_vis->InitializeAsDefaultChaseCamera(agent->GetVehicle());
        vis_manager->AddVisualization(irr_vis);
    }
#endif

#ifdef CHRONO_SENSOR
    std::shared_ptr<ChCameraSensor> intersection_camera;
    ChVector<double> camera_loc(20, -85, 15);
    if (cli.HasValueInVector<int>("sens", rank)) {
        auto sen_vis = chrono_types::make_shared<SynSensorVisualization>();

        auto manager = chrono_types::make_shared<ChSensorManager>(agent->GetSystem());
        manager->scene->AddPointLight({100, 100, 100}, {1, 1, 1}, 6000);
        manager->scene->AddPointLight({-100, 100, 100}, {1, 1, 1}, 6000);
        sen_vis->SetSensorManager(manager);

        auto origin = chrono_types::make_shared<ChBody>();
        origin->SetBodyFixed(true);
        agent->GetSystem()->AddBody(origin);

        // double theta = acos((camera_loc ^ chrono::VECT_X) / (camera_loc.Length() * 1));
        // ChVector<> rot_axis = chrono::VECT_X % (-camera_loc);
        // ChQuaternion<> rotation = Q_from_AngAxis(theta, rot_axis);

        // Rotations to get a nice angle
        ChQuaternion<> rotation = QUNIT;
        // ChQuaternion<> qA = Q_from_AngAxis(45 * CH_C_DEG_TO_RAD, VECT_Y);
        // ChQuaternion<> qB = Q_from_AngAxis(135 * CH_C_DEG_TO_RAD, VECT_Z);
        ChQuaternion<> qA = Q_from_AngAxis(30 * CH_C_DEG_TO_RAD, VECT_Y);
        ChQuaternion<> qB = Q_from_AngAxis(135 * CH_C_DEG_TO_RAD, VECT_Z);
        rotation = rotation >> qA >> qB;

        double cam_res_width = 1280;
        double cam_res_height = 720;
        double cx = camera_loc.x();
        double cy = camera_loc.y();
        double cz = camera_loc.z();
        ChVector<double> camera_temp(cx, cy, cz);
        intersection_camera = chrono_types::make_shared<chrono::sensor::ChCameraSensor>(
            origin,                                          // body camera is attached to
            30,                                              // update rate in Hz
            chrono::ChFrame<double>(camera_temp, rotation),  // offset pose
            cam_res_width,                                   // image width
            cam_res_height,                                  // image height
            CH_C_PI / 3,                                     // FOV
            1,                                               // samples per pixel for antialiasing
            PINHOLE);                                        // camera type

        intersection_camera->SetName("Intersection Cam");
        intersection_camera->PushFilter(chrono_types::make_shared<ChFilterRGBA8Access>());

        if (cli.GetAsType<bool>("sens_vis"))
            intersection_camera->PushFilter(
                chrono_types::make_shared<ChFilterVisualize>(cam_res_width, cam_res_height, "Main Camera"));

        std::string path = std::string("SENSOR_OUTPUT/Highway") + std::to_string(rank) + std::string("/");
        if (cli.GetAsType<bool>("sens_save")) {
            intersection_camera->PushFilter(chrono_types::make_shared<ChFilterSave>(path));
        }

        sen_vis->SetSensor(intersection_camera);
        vis_manager->AddVisualization(sen_vis);
    }
#endif

    mpi_manager.Initialize();

    // Simulation Loop
    while (mpi_manager.IsOk()) {
        mpi_manager.Advance();
        mpi_manager.Synchronize();

#ifdef SENSOR
        if (cli.HasValueInVector<int>("sens", rank)) {
            //  std::cout << step <<"  " << step* HEARTBEAT << std::endl;
            camera_loc += ChVector<>(0, HEARTBEAT * 7, 0);
            ChQuaternion<> rotation = QUNIT;
            ChQuaternion<> qA = Q_from_AngAxis(30 * CH_C_DEG_TO_RAD, VECT_Y);
            ChQuaternion<> qB = Q_from_AngAxis(135 * CH_C_DEG_TO_RAD, VECT_Z);
            rotation = rotation >> qA >> qB;
            double cx = camera_loc.x();
            double cy = camera_loc.y();
            double cz = camera_loc.z();
            ChVector<double> camera_temp(cx, cy, cz);
            intersection_camera->SetOffsetPose(chrono::ChFrame<double>(camera_temp, rotation));
        }
#endif  // SENSOR

        if (rank == 0 && std::abs(agent->GetSystem()->GetChTime() - 6) < 1e-2)
            std::dynamic_pointer_cast<ChMulPathFollowerACCDriver>(driver)->changePath(1);

        mpi_manager.Update();
    }

    return 0;
}