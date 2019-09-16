// =============================================================================
// PROJECT CHRONO - http://projectchrono.org
//
// Copyright (c) 2014 projectchrono.org
// All right reserved.
//
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file at the top level of the distribution and at
// http://projectchrono.org/license-chrono.txt.
//
// =============================================================================
// Authors: Radu Serban, Asher Elmquist, Evan Hoerl, Shuo He
// =============================================================================
//
// Wrapper classes for modeling an entire CityBus vehicle assembly
// (including the vehicle itself, the powertrain, and the tires).
//
// =============================================================================

#include "chrono/ChConfig.h"

#include "chrono_vehicle/ChVehicleModelData.h"

#include "chrono_models/vehicle/citybus/CityBus.h"

namespace chrono {
namespace vehicle {
namespace citybus {

// -----------------------------------------------------------------------------
CityBus::CityBus()
    : m_system(nullptr),
      m_vehicle(nullptr),
      m_powertrain(nullptr),
      m_contactMethod(ChMaterialSurface::NSC),
      m_chassisCollisionType(ChassisCollisionType::NONE),
      m_fixed(false),
      m_tireType(TireModelType::TMEASY),
      m_vehicle_step_size(-1),
      m_tire_step_size(-1),
      m_initFwdVel(0),
      m_initPos(ChCoordsys<>(ChVector<>(0, 0, 1), QUNIT)),
      m_initOmega({0, 0, 0, 0}),
      m_apply_drag(false) {}

CityBus::CityBus(ChSystem* system)
    : m_system(system),
      m_vehicle(nullptr),
      m_powertrain(nullptr),
      m_contactMethod(ChMaterialSurface::NSC),
      m_chassisCollisionType(ChassisCollisionType::NONE),
      m_fixed(false),
      m_tireType(TireModelType::RIGID),
      m_vehicle_step_size(-1),
      m_tire_step_size(-1),
      m_initFwdVel(0),
      m_initPos(ChCoordsys<>(ChVector<>(0, 0, 1), QUNIT)),
      m_initOmega({0, 0, 0, 0}),
      m_apply_drag(false) {}

CityBus::~CityBus() {
    delete m_vehicle;
    delete m_powertrain;
}

// -----------------------------------------------------------------------------
void CityBus::SetAerodynamicDrag(double Cd, double area, double air_density) {
    m_Cd = Cd;
    m_area = area;
    m_air_density = air_density;

    m_apply_drag = true;
}

// -----------------------------------------------------------------------------
void CityBus::Initialize() {
    // Create and initialize the CityBus vehicle
    m_vehicle = m_system ? new CityBus_Vehicle(m_system, m_fixed, m_chassisCollisionType)
                         : new CityBus_Vehicle(m_fixed, m_contactMethod, m_chassisCollisionType);

    m_vehicle->SetInitWheelAngVel(m_initOmega);
    m_vehicle->Initialize(m_initPos, m_initFwdVel);

    if (m_vehicle_step_size > 0) {
        m_vehicle->SetStepsize(m_vehicle_step_size);
    }

    // If specified, enable aerodynamic drag
    if (m_apply_drag) {
        m_vehicle->GetChassis()->SetAerodynamicDrag(m_Cd, m_area, m_air_density);
    }

    // Create and initialize the powertrain system
    m_powertrain = new CityBus_SimpleMapPowertrain("Powertrain");
    m_powertrain->Initialize(GetChassisBody(), m_vehicle->GetDriveshaft());

    // Create the tires and set parameters depending on type.
    switch (m_tireType) {
        case TireModelType::RIGID_MESH:
        case TireModelType::RIGID: {
            bool use_mesh = (m_tireType == TireModelType::RIGID_MESH);

            auto tire_FL = chrono_types::make_shared<CityBus_RigidTire>("FL", use_mesh);
            auto tire_FR = chrono_types::make_shared<CityBus_RigidTire>("FR", use_mesh);
            auto tire_RL = chrono_types::make_shared<CityBus_RigidTire>("RL", use_mesh);
            auto tire_RR = chrono_types::make_shared<CityBus_RigidTire>("RR", use_mesh);

            m_vehicle->InitializeTire(tire_FL, m_vehicle->GetAxle(0)->m_wheels[LEFT]);
            m_vehicle->InitializeTire(tire_FR, m_vehicle->GetAxle(0)->m_wheels[RIGHT]);
            m_vehicle->InitializeTire(tire_RL, m_vehicle->GetAxle(1)->m_wheels[LEFT]);
            m_vehicle->InitializeTire(tire_RR, m_vehicle->GetAxle(1)->m_wheels[RIGHT]);

            m_tire_mass = tire_FL->ReportMass();

            break;
        }

        case TireModelType::TMEASY: {
            auto tire_FL = chrono_types::make_shared<CityBus_TMeasyTire>("FL");
            auto tire_FR = chrono_types::make_shared<CityBus_TMeasyTire>("FR");
            auto tire_RL = chrono_types::make_shared<CityBus_TMeasyTire>("RL");
            auto tire_RR = chrono_types::make_shared<CityBus_TMeasyTire>("RR");

            m_vehicle->InitializeTire(tire_FL, m_vehicle->GetAxle(0)->m_wheels[LEFT]);
            m_vehicle->InitializeTire(tire_FR, m_vehicle->GetAxle(0)->m_wheels[RIGHT]);
            m_vehicle->InitializeTire(tire_RL, m_vehicle->GetAxle(1)->m_wheels[LEFT]);
            m_vehicle->InitializeTire(tire_RR, m_vehicle->GetAxle(1)->m_wheels[RIGHT]);

            m_tire_mass = tire_FL->ReportMass();

            break;
        }
        default:
            break;
    }

    for (auto& axle : m_vehicle->GetAxles()) {
        for (auto& wheel : axle->GetWheels()) {
            if (m_tire_step_size > 0)
                wheel->GetTire()->SetStepsize(m_tire_step_size);
        }
    }
}

// -----------------------------------------------------------------------------
void CityBus::SetTireVisualizationType(VisualizationType vis) {
    for (auto& axle : m_vehicle->GetAxles()) {
        for (auto& wheel : axle->GetWheels()) {
            wheel->GetTire()->SetVisualizationType(vis);
        }
    }
}

// -----------------------------------------------------------------------------
void CityBus::Synchronize(double time,
                      double steering_input,
                      double braking_input,
                      double throttle_input,
                      const ChTerrain& terrain) {
    double powertrain_torque = m_powertrain->GetOutputTorque();
    double driveshaft_speed = m_vehicle->GetDriveshaftSpeed();

    m_powertrain->Synchronize(time, throttle_input, driveshaft_speed);
    m_vehicle->Synchronize(time, steering_input, braking_input, powertrain_torque, terrain);
}

// -----------------------------------------------------------------------------
void CityBus::Advance(double step) {
    m_powertrain->Advance(step);
    m_vehicle->Advance(step);
}

// -----------------------------------------------------------------------------
double CityBus::GetTotalMass() const {
    return m_vehicle->GetVehicleMass() + 4 * m_tire_mass;
}

}  // end namespace citybus
}  // end namespace vehicle
}  // end namespace chrono
