/* Copyright (c) <2009> <Newton Game Dynamics>
* 
* This software is provided 'as-is', without any express or implied
* warranty. In no event will the authors be held liable for any damages
* arising from the use of this software.
* 
* Permission is granted to anyone to use this software for any purpose,
* including commercial applications, and to alter it and redistribute it
* freely
*/


// NewtonCustomJoint.cpp: implementation of the NewtonCustomJoint class.
//
//////////////////////////////////////////////////////////////////////
#include "CustomJointLibraryStdAfx.h"
#include "CustomJoint.h"
#include "CustomHinge.h"
#include "CustomVehicleControllerManager.h"

//#define D_PLOT_ENGINE_CURVE

#ifdef D_PLOT_ENGINE_CURVE 
static FILE* file_xxx;
#endif


#define D_VEHICLE_NEUTRAL_GEAR		0
#define D_VEHICLE_REVERSE_GEAR		1
#define D_VEHICLE_FIRST_GEAR		2

static int xxx;

/*
class CustomVehicleController::dWeightDistibutionSolver: public dSymmetricBiconjugateGradientSolve
{
	public:
	dWeightDistibutionSolver()
		:dSymmetricBiconjugateGradientSolve()
		,m_count(0)
	{
	}

	virtual void MatrixTimeVector(dFloat64* const out, const dFloat64* const v) const
	{
		dComplentaritySolver::dJacobian invMassJacobians;
		invMassJacobians.m_linear = dVector(0.0f, 0.0f, 0.0f, 0.0f);
		invMassJacobians.m_angular = dVector(0.0f, 0.0f, 0.0f, 0.0f);
		for (int i = 0; i < m_count; i++) {
			invMassJacobians.m_linear += m_invMassJacobians[i].m_linear.Scale(dFloat(v[i]));
			invMassJacobians.m_angular += m_invMassJacobians[i].m_angular.Scale(dFloat(v[i]));
		}

		for (int i = 0; i < m_count; i++) {
			out[i] = m_diagRegularizer[i] * v[i] + invMassJacobians.m_linear % m_jacobians[i].m_linear + invMassJacobians.m_angular % m_jacobians[i].m_angular;
		}
	}

	virtual bool InversePrecoditionerTimeVector(dFloat64* const out, const dFloat64* const v) const
	{
		for (int i = 0; i < m_count; i++) {
			out[i] = v[i] * m_invDiag[i];
		}
		return true;
	}

	dComplentaritySolver::dJacobian m_jacobians[256];
	dComplentaritySolver::dJacobian m_invMassJacobians[256];
	dFloat m_invDiag[256];
	dFloat m_diagRegularizer[256];
	int m_count;
};
*/


void CustomVehicleController::dInterpolationCurve::InitalizeCurve(int points, const dFloat* const steps, const dFloat* const values)
{
	m_count = points;
	dAssert(points < int(sizeof(m_nodes) / sizeof (m_nodes[0])));
	memset(m_nodes, 0, sizeof (m_nodes));
	for (int i = 0; i < m_count; i++) {
		m_nodes[i].m_param = steps[i];
		m_nodes[i].m_value = values[i];
	}
}

dFloat CustomVehicleController::dInterpolationCurve::GetValue(dFloat param) const
{
	dFloat interplatedValue = 0.0f;
	if (m_count) {
		param = dClamp(param, 0.0f, m_nodes[m_count - 1].m_param);
		interplatedValue = m_nodes[m_count - 1].m_value;
		for (int i = 1; i < m_count; i++) {
			if (param < m_nodes[i].m_param) {
				dFloat df = m_nodes[i].m_value - m_nodes[i - 1].m_value;
				dFloat ds = m_nodes[i].m_param - m_nodes[i - 1].m_param;
				dFloat step = param - m_nodes[i - 1].m_param;

				interplatedValue = m_nodes[i - 1].m_value + df * step / ds;
				break;
			}
		}
	}
	return interplatedValue;
}

class CustomVehicleController::WheelJoint: public CustomJoint
{
	public:
	WheelJoint (const dMatrix& pinAndPivotFrame, NewtonBody* const tire, NewtonBody* const parentBody, BodyPartTire* const tireData)
		:CustomJoint (6, tire, parentBody)
		,m_lateralDir(0.0f, 0.0f, 0.0f, 0.0f)
		,m_longitudinalDir(0.0f, 0.0f, 0.0f, 0.0f)
		,m_tire (tireData)
		,m_tireLoad(0.0f)
		,m_steerRate (0.25f * 3.1416f)
		,m_steerAngle0(0.0f)
		,m_steerAngle1(0.0f)
		,m_brakeTorque(0.0f)
	{
		CalculateLocalMatrix (pinAndPivotFrame, m_localMatrix0, m_localMatrix1);
	}

	dFloat CalcuateTireParametricPosition(const dMatrix& tireMatrix, const dMatrix& chassisMatrix) const 
	{
		const dVector& chassisP0 = chassisMatrix.m_posit;
		dVector chassisP1(chassisMatrix.m_posit + chassisMatrix.m_up.Scale(m_tire->m_data.m_suspesionlenght));
		dVector p1p0(chassisP1 - chassisP0);
		dVector q1p0(tireMatrix.m_posit - chassisP0);
		dFloat num = q1p0 % p1p0;
		dFloat den = p1p0 % p1p0;
		return num / den;
	}

	void RemoveKinematicError(dFloat timestep)
	{
		dMatrix tireMatrix;
		dMatrix chassisMatrix;
		dVector tireVeloc;
		dVector tireOmega;
		dVector chassisVeloc;
		dVector chassisOmega;

		CalculateGlobalMatrix(tireMatrix, chassisMatrix);

		if (m_steerAngle0 < m_steerAngle1) {
			m_steerAngle0 += m_steerRate * timestep;
			if (m_steerAngle0 > m_steerAngle1) {
				m_steerAngle0 = m_steerAngle1;
			}
		} else if (m_steerAngle0 > m_steerAngle1) {
			m_steerAngle0 -= m_steerRate * timestep;
			if (m_steerAngle0 < m_steerAngle1) {
				m_steerAngle0 = m_steerAngle1;
			}
		}

		chassisMatrix = dYawMatrix(m_steerAngle0) * chassisMatrix;

		tireMatrix.m_front = chassisMatrix.m_front;
		tireMatrix.m_right = tireMatrix.m_front * tireMatrix.m_up;
		tireMatrix.m_right = tireMatrix.m_right.Scale(1.0f / dSqrt(tireMatrix.m_right % tireMatrix.m_right));
		tireMatrix.m_up = tireMatrix.m_right * tireMatrix.m_front;

		dFloat param = CalcuateTireParametricPosition (tireMatrix, chassisMatrix);
		tireMatrix.m_posit = chassisMatrix.m_posit + chassisMatrix.m_up.Scale (param * m_tire->m_data.m_suspesionlenght);

		NewtonBody* const tire = m_body0;
		NewtonBody* const chassis = m_body1;

		tireMatrix = GetMatrix0().Inverse() * tireMatrix;
		NewtonBodyGetVelocity(tire, &tireVeloc[0]);
		NewtonBodyGetPointVelocity(chassis, &tireMatrix.m_posit[0], &chassisVeloc[0]);
		chassisVeloc -= chassisMatrix.m_up.Scale (chassisVeloc % chassisMatrix.m_up);
		tireVeloc = chassisVeloc + chassisMatrix.m_up.Scale (tireVeloc % chassisMatrix.m_up);
		
		NewtonBodyGetOmega(tire, &tireOmega[0]);
		NewtonBodyGetOmega(chassis, &chassisOmega[0]);
		tireOmega = chassisOmega + tireMatrix.m_front.Scale (tireOmega % tireMatrix.m_front);

		NewtonBodySetMatrixNoSleep(tire, &tireMatrix[0][0]);
		NewtonBodySetVelocityNoSleep(tire, &tireVeloc[0]);
		NewtonBodySetOmegaNoSleep(tire, &tireOmega[0]);
	}

	void SubmitConstraints(dFloat timestep, int threadIndex)
	{
		dMatrix tireMatrix;
		dMatrix chassisMatrix;
		dVector tireOmega;
		dVector chassisOmega;
		dVector tireVeloc;
		dVector chassisCom;
		dVector chassisVeloc;

		NewtonBody* const tire = m_body0;
		NewtonBody* const chassis = m_body1;
		dAssert (m_body0 == m_tire->GetBody());
		dAssert (m_body1 == m_tire->GetParent()->GetBody());

		// calculate the position of the pivot point and the Jacobian direction vectors, in global space. 
		CalculateGlobalMatrix(tireMatrix, chassisMatrix);
		chassisMatrix = dYawMatrix(m_steerAngle0) * chassisMatrix;

		m_lateralDir = chassisMatrix.m_front;
		m_longitudinalDir = chassisMatrix.m_right;

		NewtonUserJointAddLinearRow(m_joint, &tireMatrix.m_posit[0], &chassisMatrix.m_posit[0], &m_lateralDir[0]);
		NewtonUserJointSetRowAcceleration(m_joint, NewtonUserCalculateRowZeroAccelaration(m_joint));

		NewtonUserJointAddLinearRow(m_joint, &tireMatrix.m_posit[0], &chassisMatrix.m_posit[0], &m_longitudinalDir[0]);
		NewtonUserJointSetRowAcceleration(m_joint, NewtonUserCalculateRowZeroAccelaration(m_joint));
		
		NewtonBodyGetOmega(tire, &tireOmega[0]);
		NewtonBodyGetOmega(chassis, &chassisOmega[0]);
		dVector relOmega(tireOmega - chassisOmega);

		dFloat angle = -CalculateAngle(tireMatrix.m_front, chassisMatrix.m_front, chassisMatrix.m_right);
		dFloat omega = relOmega % chassisMatrix.m_right;
		dFloat alphaError = -(angle + omega * timestep) / (timestep * timestep);
		NewtonUserJointAddAngularRow(m_joint, -angle, &chassisMatrix.m_right[0]);
		NewtonUserJointSetRowAcceleration(m_joint, alphaError);

		angle = CalculateAngle(tireMatrix.m_front, chassisMatrix.m_front, chassisMatrix.m_up);
		omega = relOmega % chassisMatrix.m_up;
		alphaError = -(angle + omega * timestep) / (timestep * timestep);
		NewtonUserJointAddAngularRow(m_joint, -angle, &chassisMatrix.m_up[0]);
		NewtonUserJointSetRowAcceleration(m_joint, alphaError);

		dFloat param = CalcuateTireParametricPosition(tireMatrix, chassisMatrix);
		if (param >= 1.0f) {
			dVector chassisMatrixPosit (chassisMatrix.m_posit + chassisMatrix.m_up.Scale (param * m_tire->m_data.m_suspesionlenght));
			NewtonUserJointAddLinearRow(m_joint, &tireMatrix.m_posit[0], &chassisMatrix.m_posit[0], &chassisMatrix.m_up[0]);
			NewtonUserJointSetRowSpringDamperAcceleration(m_joint, m_tire->m_data.m_springStrength, m_tire->m_data.m_dampingRatio);
			NewtonUserJointSetRowMaximumFriction(m_joint, 0.0f);
		} else if (param <= 0.0f) {
			NewtonUserJointAddLinearRow(m_joint, &tireMatrix.m_posit[0], &chassisMatrix.m_posit[0], &chassisMatrix.m_up[0]);
			NewtonUserJointSetRowMinimumFriction(m_joint, 0.0f);
		} else {
			NewtonUserJointAddLinearRow(m_joint, &tireMatrix.m_posit[0], &chassisMatrix.m_posit[0], &chassisMatrix.m_up[0]);
			NewtonUserJointSetRowSpringDamperAcceleration(m_joint, m_tire->m_data.m_springStrength, m_tire->m_data.m_dampingRatio);
		}

		if (m_brakeTorque > 1.0e-3f) {
			dFloat speed = relOmega % m_lateralDir;
			NewtonUserJointAddAngularRow(m_joint, 0.0f, &m_lateralDir[0]);
			NewtonUserJointSetRowAcceleration(m_joint, -speed / timestep);
			NewtonUserJointSetRowMinimumFriction(m_joint, -m_brakeTorque);
			NewtonUserJointSetRowMaximumFriction(m_joint, m_brakeTorque);
		}
		m_brakeTorque = 0.0f;
	}

	dFloat GetTireLoad () const
	{
		return m_tireLoad;
	}

	dVector GetLongitudinalForce() const
	{
		return m_longitudinalDir.Scale(NewtonUserJointGetRowForce(m_joint, 1));
	}

	dVector GetLateralForce() const
	{
		return m_lateralDir.Scale (NewtonUserJointGetRowForce (m_joint, 0));
	}

	dVector m_lateralDir;
	dVector m_longitudinalDir;
	BodyPartTire* m_tire;
	dFloat m_tireLoad;
	dFloat m_steerRate;
	dFloat m_steerAngle0;
	dFloat m_steerAngle1;
	dFloat m_brakeTorque;
};

/*
class CustomVehicleController::EngineJoint: public CustomJoint
{
	public:
	EngineJoint(NewtonBody* const engine, NewtonBody* const chassis)
		:CustomJoint(6, engine, chassis)
		,m_fowardDryFriction (0.0f)
		,m_reverseDryFriction(0.0f)
	{
		ResetLocatMatrix();
	}

	void ResetLocatMatrix()
	{
		dMatrix engineMatrix;
		NewtonBodyGetMatrix(GetBody0(), &engineMatrix[0][0]);
		CalculateLocalMatrix(engineMatrix, m_localMatrix0, m_localMatrix1);
	}

	void RemoveKinematicError()
	{
		dMatrix engineMatrix;
		dMatrix chassisMatrix;
		dVector engineVeloc;
		dVector engineOmega;
		dVector chassisOmega;
		NewtonBody* const engine = m_body0;
		NewtonBody* const chassis = m_body1;

		CalculateGlobalMatrix(engineMatrix, chassisMatrix);
		NewtonBodyGetOmega(engine, &engineOmega[0]);
		NewtonBodyGetOmega(chassis, &chassisOmega[0]);
		NewtonBodyGetPointVelocity(chassis, &engineMatrix.m_posit[0], &engineVeloc[0]);

		engineMatrix = GetMatrix0().Inverse() * chassisMatrix;

		dVector relOmega (engineOmega - chassisOmega);
		engineOmega = chassisOmega + chassisMatrix.m_front.Scale (relOmega % chassisMatrix.m_front);

		NewtonBodySetMatrixNoSleep(engine, &engineMatrix[0][0]);
		NewtonBodySetVelocityNoSleep(engine, &engineVeloc[0]);
		NewtonBodySetOmegaNoSleep(engine, &engineOmega[0]);
	}

	void SubmitConstraints(dFloat timestep, int threadIndex)
	{
		dMatrix engineMatrix;
		dMatrix chassisMatrix;

		// calculate the position of the pivot point and the Jacobian direction vectors, in global space. 
		CalculateGlobalMatrix(engineMatrix, chassisMatrix);

		// Restrict the movement on the pivot point along all tree orthonormal direction
		NewtonUserJointAddLinearRow(m_joint, &engineMatrix.m_posit[0], &chassisMatrix.m_posit[0], &chassisMatrix.m_front[0]);
		NewtonUserJointSetRowAcceleration (m_joint, NewtonUserCalculateRowZeroAccelaration(m_joint));
		NewtonUserJointAddLinearRow(m_joint, &engineMatrix.m_posit[0], &chassisMatrix.m_posit[0], &chassisMatrix.m_up[0]);
		NewtonUserJointSetRowAcceleration (m_joint, NewtonUserCalculateRowZeroAccelaration(m_joint));
		NewtonUserJointAddLinearRow(m_joint, &engineMatrix.m_posit[0], &chassisMatrix.m_posit[0], &chassisMatrix.m_right[0]);
		NewtonUserJointSetRowAcceleration (m_joint, NewtonUserCalculateRowZeroAccelaration(m_joint));

		chassisMatrix.m_front = engineMatrix.m_front - chassisMatrix.m_up.Scale(engineMatrix.m_front % chassisMatrix.m_up);
		chassisMatrix.m_front = chassisMatrix.m_front.Scale(1.0f / dSqrt(chassisMatrix.m_front % chassisMatrix.m_front));
		chassisMatrix.m_right = chassisMatrix.m_front * chassisMatrix.m_up;

		dVector omega0;
		dVector omega1;
		NewtonBodyGetOmega(m_body0, &omega0[0]);
		NewtonBodyGetOmega(m_body1, &omega1[0]);
		dVector relOmega(omega0 - omega1);

		dFloat angle = -CalculateAngle(engineMatrix.m_front, chassisMatrix.m_front, chassisMatrix.m_right);
		dFloat omega = (relOmega % chassisMatrix.m_right);
		dFloat alphaError = -(angle + omega * timestep) / (timestep * timestep);
		NewtonUserJointAddAngularRow(m_joint, -angle, &chassisMatrix.m_right[0]);
		NewtonUserJointSetRowAcceleration(m_joint, alphaError);

		dFloat longOmega = relOmega % engineMatrix.m_front;
		dVector drag(0.0f, 0.0f, 0.0f, 0.0f);
		NewtonBodySetAngularDamping(m_body0, &drag[0]);
		NewtonUserJointAddAngularRow(m_joint, 0.0f, &engineMatrix.m_front[0]);
		NewtonUserJointSetRowAcceleration(m_joint, -longOmega / timestep);
		NewtonUserJointSetRowMinimumFriction(m_joint, -m_fowardDryFriction);
		NewtonUserJointSetRowMaximumFriction(m_joint, m_reverseDryFriction);
	}

	dFloat m_fowardDryFriction;
	dFloat m_reverseDryFriction;
};


class CustomVehicleController::DifferentialSpiderGearJoint: public CustomJoint
{
	public:
	DifferentialSpiderGearJoint(EngineJoint* const engineJoint, WheelJoint* const tireJoint)
		:CustomJoint(3, tireJoint->GetBody0(), engineJoint->GetBody0())
		,m_gearGain(0.0f)
		,m_clutchTorque(D_CUSTOM_LARGE_VALUE)
		,m_couplingFactor (0.6f)
	{
		dMatrix matrix;
		dMatrix tireMatrix;
		dMatrix engineMatrix;

		NewtonBody* const tire = GetBody0();
		NewtonBody* const engine = GetBody1();

		NewtonBodyGetMatrix(tire, &tireMatrix[0][0]);
		NewtonBodyGetMatrix(engine, &engineMatrix[0][0]);
		tireMatrix = tireJoint->GetMatrix0() * tireMatrix;
		engineMatrix = engineJoint->GetMatrix0() * engineMatrix;

		CalculateLocalMatrix(tireMatrix, m_localMatrix0, matrix);
		CalculateLocalMatrix(engineMatrix, matrix, m_localMatrix1);

		m_diffSign = dSign((tireMatrix.m_posit - engineMatrix.m_posit) % engineMatrix[0]);
	}

	void SubmitConstraints(dFloat timestep, int threadIndex)
	{
		dMatrix tireMatrix;
		dMatrix engineMatrix;
		dVector tireOmega;
		dVector engineOmega;
		dFloat jacobian0[6];
		dFloat jacobian1[6];

		NewtonBody* const tire = m_body0;
		NewtonBody* const engine = m_body1;

		NewtonBodyGetOmega(tire, &tireOmega[0]);
		NewtonBodyGetOmega(engine, &engineOmega[0]);
		CalculateGlobalMatrix(tireMatrix, engineMatrix);

		jacobian0[0] = 0.0f;
		jacobian0[1] = 0.0f;
		jacobian0[2] = 0.0f;
		jacobian0[3] = tireMatrix.m_front.m_x;
		jacobian0[4] = tireMatrix.m_front.m_y;
		jacobian0[5] = tireMatrix.m_front.m_z;
		jacobian1[0] = 0.0f;
		jacobian1[1] = 0.0f;
		jacobian1[2] = 0.0f;
		jacobian1[3] = engineMatrix.m_front.m_x;
		jacobian1[4] = engineMatrix.m_front.m_y;
		jacobian1[5] = engineMatrix.m_front.m_z;

		dFloat engineSpeed = engineOmega % engineMatrix.m_front;
		dFloat tireSpeed = (tireOmega % tireMatrix.m_front) * m_gearGain;
		//dFloat differentialSpeed = (engineOmega % engineMatrix.m_up) * m_gearGain * m_diffSign;
		dFloat differentialSpeed = 0.0f;

		dFloat relSpeed = tireSpeed + engineSpeed + differentialSpeed;
		NewtonUserJointAddGeneralRow(m_joint, &jacobian0[0], &jacobian1[0]);
		NewtonUserJointSetRowAcceleration(m_joint, -relSpeed * m_couplingFactor / timestep);

		if (m_clutchTorque < (D_CUSTOM_LARGE_VALUE * 0.1f)) {
			NewtonUserJointSetRowMinimumFriction(m_joint, -m_clutchTorque);
			NewtonUserJointSetRowMaximumFriction(m_joint, m_clutchTorque);
		}
	}

	void SetGain(dFloat gain)
	{
		m_gearGain = gain;
	}

	void SetClutch(dFloat torque)
	{
		m_clutchTorque = dClamp(torque, dFloat(0.1f), D_CUSTOM_LARGE_VALUE);
	}

	dFloat m_diffSign;
	dFloat m_gearGain;
	dFloat m_clutchTorque;
	dFloat m_couplingFactor;
};
*/

void CustomVehicleController::BodyPartChassis::ApplyDownForce ()
{
	// add aerodynamics forces
	dMatrix matrix;
	dVector veloc;

	NewtonBody* const body = GetBody();
	NewtonBodyGetVelocity(body, &veloc[0]);
	NewtonBodyGetMatrix(body, &matrix[0][0]);

	matrix = GetController()->m_localFrame * matrix;

	veloc -= matrix.m_up.Scale (veloc % matrix.m_up);
	//dVector downforce (matrix.m_up.Scale(-m_aerodynamicsDownForceCoefficient * (veloc % veloc)));
	dVector downforce(matrix.m_up.Scale(-10.0f * (veloc % veloc)));
	NewtonBodyAddForce(body, &downforce[0]);
}

CustomVehicleController::BodyPartTire::BodyPartTire()
	:BodyPart()
	,m_lateralSlip(0.0f)
	,m_longitudinalSlip(0.0f)
	,m_aligningTorque(0.0f)
	,m_index(0)
	,m_collidingCount(0)
{
}

CustomVehicleController::BodyPartTire::~BodyPartTire()
{
}

void CustomVehicleController::BodyPartTire::Init (BodyPart* const parentPart, const dMatrix& locationInGlobalSpase, const Info& info)
{
	m_data = info;
	m_parent = parentPart;
	m_userData = info.m_userData;
	m_controller = parentPart->m_controller;

	m_collidingCount = 0;
	m_lateralSlip = 0.0f;
	m_aligningTorque = 0.0f;
	m_longitudinalSlip = 0.0f;

	CustomVehicleControllerManager* const manager = (CustomVehicleControllerManager*)m_controller->GetManager();

	NewtonWorld* const world = ((CustomVehicleControllerManager*)m_controller->GetManager())->GetWorld();
	NewtonCollisionSetScale(manager->m_tireShapeTemplate, m_data.m_width, m_data.m_radio, m_data.m_radio);

	// create the rigid body that will make this bone
	dMatrix matrix (dYawMatrix(-0.5f * 3.1415927f) * locationInGlobalSpase);
	m_body = NewtonCreateDynamicBody(world, manager->m_tireShapeTemplate, &matrix[0][0]);
	NewtonCollision* const collision = NewtonBodyGetCollision(m_body);
	NewtonCollisionSetUserData1 (collision, this);
	
	NewtonBodySetMaterialGroupID(m_body, manager->GetTireMaterial());

	dVector drag(0.0f, 0.0f, 0.0f, 0.0f);
	NewtonBodySetLinearDamping(m_body, 0);
	NewtonBodySetAngularDamping(m_body, &drag[0]);
	NewtonBodySetMaxRotationPerStep(m_body, 3.141692f);
	
	// set the standard force and torque call back
	NewtonBodySetForceAndTorqueCallback(m_body, m_controller->m_forceAndTorque);

	// tire are highly non linear, sung spherical inertia matrix make the calculation more accurate 
	dFloat inertia = 2.0f * m_data.m_mass * m_data.m_radio * m_data.m_radio / 5.0f;
	NewtonBodySetMassMatrix (m_body, m_data.m_mass, inertia, inertia, inertia);

	m_joint = new WheelJoint (matrix, m_body, parentPart->m_body, this);
}

dFloat CustomVehicleController::BodyPartTire::GetRPM() const
{
	dVector omega; 
	WheelJoint* const joint = (WheelJoint*) m_joint;
	NewtonBodyGetOmega(m_body, &omega[0]);
	return (joint->m_lateralDir % omega) * 9.55f;
}


void CustomVehicleController::BodyPartTire::SetSteerAngle (dFloat angle)
{
	WheelJoint* const tire = (WheelJoint*)m_joint;
	tire->m_steerAngle1 = angle;
}

void CustomVehicleController::BodyPartTire::SetBrakeTorque(dFloat torque)
{
	WheelJoint* const tire = (WheelJoint*)m_joint;
	tire->m_brakeTorque = dMax (torque, tire->m_brakeTorque);
}

/*
CustomVehicleController::BodyPartEngine::BodyPartEngine (CustomVehicleController* const controller, const Info& info, const DifferentialAxel& axel0, const DifferentialAxel& axel1)
	:BodyPart()
	,m_info(info)
	,m_infoCopy(info)
	,m_differential0(axel0)
	,m_differential1(axel1)
	,m_norminalTorque(0.0f)
	,m_currentGear(2)
	,m_gearTimer(0)
{
	m_parent = &controller->m_chassis;
	m_controller = controller;

	dAssert (axel0.m_leftTire);
	dAssert (axel0.m_rightTire);

	NewtonWorld* const world = ((CustomVehicleControllerManager*)m_controller->GetManager())->GetWorld();

	//NewtonCollision* const collision = NewtonCreateNull(world);
	NewtonCollision* const collision = NewtonCreateSphere(world, 0.1f, 0, NULL);
	//NewtonCollision* const collision = NewtonCreateCylinder(world, 0.5f, 0.5f, 0, NULL);

	dMatrix engineMatrix (CalculateEngineMatrix());
	m_body = NewtonCreateDynamicBody(world, collision, &engineMatrix[0][0]);
	NewtonDestroyCollision(collision);

	EngineJoint* const engineJoint = new EngineJoint(m_body, m_parent->GetBody());
	m_joint = engineJoint;

	m_differential0.m_leftGear = new DifferentialSpiderGearJoint (engineJoint, (WheelJoint*)m_differential0.m_leftTire->GetJoint());
	m_differential0.m_rightGear = new DifferentialSpiderGearJoint (engineJoint, (WheelJoint*)m_differential0.m_rightTire->GetJoint());
	if (m_differential1.m_leftTire) {
		m_differential1.m_leftGear = new DifferentialSpiderGearJoint (engineJoint, (WheelJoint*)m_differential1.m_leftTire->GetJoint());
		m_differential1.m_rightGear = new DifferentialSpiderGearJoint (engineJoint, (WheelJoint*)m_differential1.m_rightTire->GetJoint());
	}

	NewtonBodySetForceAndTorqueCallback(m_body, m_controller->m_forceAndTorque);
	SetInfo (info);
}

CustomVehicleController::BodyPartEngine::~BodyPartEngine()
{
}

dMatrix CustomVehicleController::BodyPartEngine::CalculateEngineMatrix() const
{
	dMatrix matrix;
	dMatrix offset(dYawMatrix(-0.5f * 3.14159213f) * m_controller->m_localFrame);
	//offset.m_posit.m_y += 0.25f;
	//offset.m_posit.m_y += 2.0f;
	//offset.m_posit.m_x -= 2.0f;

	NewtonBodyGetMatrix(m_controller->GetBody(), &matrix[0][0]);
	matrix = offset * matrix;
	matrix.m_posit += matrix.RotateVector(m_info.m_location);
	matrix.m_posit.m_w = 1.0f;
	return matrix;
}

void CustomVehicleController::BodyPartEngine::Info::ConvertToMetricSystem()
{
	const dFloat horsePowerToWatts = 735.5f;
	const dFloat kmhToMetersPerSecunds = 0.278f;
	const dFloat rpmToRadiansPerSecunds = 0.105f;
	const dFloat poundFootToNewtonMeters = 1.356f;

	m_idleTorque *= poundFootToNewtonMeters;
	m_peakTorque *= poundFootToNewtonMeters;
	m_redLineTorque *= poundFootToNewtonMeters;

	m_rpmAtPeakTorque *= rpmToRadiansPerSecunds;
	m_rpmAtPeakHorsePower *= rpmToRadiansPerSecunds;
	m_rpmAtReadLineTorque *= rpmToRadiansPerSecunds;
	m_rpmAtIdleTorque *= rpmToRadiansPerSecunds;
	
	m_peakHorsePower *= horsePowerToWatts;
	m_vehicleTopSpeed *= kmhToMetersPerSecunds;

	m_peakPowerTorque = m_peakHorsePower / m_rpmAtPeakHorsePower;

	//m_idleTorque = m_peakTorque * 0.5f;
	//m_peakPowerTorque = m_peakTorque * 0.5f;
	dAssert(m_rpmAtIdleTorque > 0.0f);
	dAssert(m_rpmAtIdleTorque < m_rpmAtPeakHorsePower);
	dAssert(m_rpmAtPeakTorque < m_rpmAtPeakHorsePower);
	dAssert(m_rpmAtPeakHorsePower < m_rpmAtReadLineTorque);

	dAssert(m_idleTorque > 0.0f);
	dAssert(m_peakTorque > m_peakPowerTorque);
	dAssert(m_peakPowerTorque > m_redLineTorque);
	dAssert(m_redLineTorque > 0.0f);
	dAssert((m_peakTorque * m_rpmAtPeakTorque) < m_peakHorsePower);
}


CustomVehicleController::BodyPartEngine::Info CustomVehicleController::BodyPartEngine::GetInfo() const
{
	return m_infoCopy;
}

void CustomVehicleController::BodyPartEngine::SetInfo(const Info& info)
{
	m_info = info;
	m_infoCopy = info;
	dFloat inertia = 2.0f * m_info.m_mass * m_info.m_radio * m_info.m_radio / 5.0f;
	NewtonBodySetMassMatrix(m_body, m_info.m_mass, inertia, inertia, inertia);

	EngineJoint* const engineJoint = (EngineJoint*) GetJoint();
	engineJoint->m_reverseDryFriction = inertia * dAbs (100.0f);

	dVector drag(0.0f, 0.0f, 0.0f, 0.0f);
	NewtonBodySetLinearDamping(m_body, 0);
	NewtonBodySetAngularDamping(m_body, &drag[0]);
	NewtonBodySetMaxRotationPerStep(m_body, 3.141692f * 2.0f);
	
	dMatrix engineMatrix (CalculateEngineMatrix());
	NewtonBodySetMatrixNoSleep(m_body, &engineMatrix[0][0]);
	engineJoint->ResetLocatMatrix();

	InitEngineTorqueCurve();
	dAssert(info.m_gearsCount < (int(sizeof (m_info.m_gearRatios) / sizeof (m_info.m_gearRatios[0])) - D_VEHICLE_FIRST_GEAR));
	m_info.m_gearsCount = info.m_gearsCount + D_VEHICLE_FIRST_GEAR;

	m_info.m_gearRatios[D_VEHICLE_NEUTRAL_GEAR] = 0.0f;
	m_info.m_gearRatios[D_VEHICLE_REVERSE_GEAR] = -dAbs(info.m_reverseGearRatio);
	for (int i = 0; i < (m_info.m_gearsCount - D_VEHICLE_FIRST_GEAR); i++) {
		m_info.m_gearRatios[i + D_VEHICLE_FIRST_GEAR] = dAbs(info.m_gearRatios[i]);
	}

	for (dList<BodyPartTire>::dListNode* tireNode = m_controller->m_tireList.GetFirst(); tireNode; tireNode = tireNode->GetNext()) {
		BodyPartTire& tire = tireNode->GetInfo();
		dFloat angle = (1.0f / 30.0f) * (0.277778f) * info.m_vehicleTopSpeed / tire.m_data.m_radio;
		NewtonBodySetMaxRotationPerStep(tire.GetBody(), angle);
	}
}


dFloat CustomVehicleController::BodyPartEngine::GetTopGear() const
{
	return m_info.m_gearRatios[m_info.m_gearsCount - 1];
}


void CustomVehicleController::BodyPartEngine::SetTopSpeed()
{
	dAssert(m_info.m_vehicleTopSpeed >= 0.0f);
	dAssert(m_info.m_vehicleTopSpeed < 100.0f);

	const BodyPartTire* const tire = m_differential0.m_leftTire;
	
	dFloat effectiveRadio = tire->m_data.m_radio;

	// drive train geometrical relations
	// G0 = m_differentialGearRatio
	// G1 = m_transmissionGearRatio
	// s = topSpeedMPS
	// r = tireRadio
	// wt = rpsAtTire
	// we = rpsAtPickPower
	// we = G1 * G0 * wt;
	// wt = e / r
	// we = G0 * G1 * s / r
	// G0 = r * we / (G1 * s)
	// using the top gear and the optimal engine torque for the calculations
	dFloat topGearRatio = GetTopGear();
	m_info.m_crownGearRatio = effectiveRadio * m_info.m_rpmAtPeakHorsePower / (m_info.m_vehicleTopSpeed * topGearRatio);
}


void CustomVehicleController::BodyPartEngine::InitEngineTorqueCurve()
{
	m_info.ConvertToMetricSystem();
	SetTopSpeed();

	dFloat rpsTable[5];
	dFloat torqueTable[5];

	rpsTable[0] = 0.0f;
	rpsTable[1] = m_info.m_rpmAtIdleTorque;
	rpsTable[2] = m_info.m_rpmAtPeakTorque;
	rpsTable[3] = m_info.m_rpmAtPeakHorsePower;
	rpsTable[4] = m_info.m_rpmAtReadLineTorque;

	torqueTable[0] = m_info.m_idleTorque;
	torqueTable[1] = m_info.m_idleTorque;
	torqueTable[2] = m_info.m_peakTorque;
	torqueTable[3] = m_info.m_peakPowerTorque;
	torqueTable[4] = m_info.m_redLineTorque;

	if (m_info.m_idleTorque * 0.25f > m_info.m_redLineTorque) {
		torqueTable[4] = m_info.m_idleTorque * 0.25f;
	}

	const int count = sizeof (rpsTable) / sizeof (rpsTable[0]);
	for (int i = 0; i < count; i++) {
		rpsTable[i] /= m_info.m_crownGearRatio;
		torqueTable[i] *= m_info.m_crownGearRatio;
	}

	m_torqueRPMCurve.InitalizeCurve(sizeof (rpsTable) / sizeof (rpsTable[0]), rpsTable, torqueTable);

	dFloat idleTorque = torqueTable[4] * 0.5f;
	EngineJoint* const engineJoint = (EngineJoint*) GetJoint();
	engineJoint->m_fowardDryFriction = idleTorque * 0.5f;

	dFloat W = rpsTable[1];
	dFloat T = idleTorque - engineJoint->m_fowardDryFriction;
	m_idleViscousFriction = T / (W * W);

	W = rpsTable[4];
	m_idleViscousFriction2 = (torqueTable[4] - idleTorque) / (W * W * W * W);
}

dFloat CustomVehicleController::BodyPartEngine::GetRedLineRPM() const
{
	return m_info.m_rpmAtReadLineTorque * 9.55f;
}

dFloat CustomVehicleController::BodyPartEngine::GetSpeed() const
{
	dMatrix matrix;
	dVector veloc;

	EngineJoint* const joint = (EngineJoint*)GetJoint();
	NewtonBody* const chassis = joint->GetBody1();

	NewtonBodyGetMatrix(chassis, &matrix[0][0]);
	NewtonBodyGetVelocity(chassis, &veloc[0]);

	matrix = joint->GetMatrix1() * matrix;
	return dAbs(matrix.m_right % veloc);
}

dFloat CustomVehicleController::BodyPartEngine::GetRPM() const
{
	dMatrix engineMatrix;
	dVector omega0;
	dVector omega1;

	EngineJoint* const joint = (EngineJoint*) GetJoint();
	NewtonBody* const engine = joint->GetBody0();
	NewtonBody* const chassis = joint->GetBody1();

	NewtonBodyGetOmega(engine, &omega0[0]);
	NewtonBodyGetOmega(chassis, &omega1[0]);
	NewtonBodyGetMatrix(engine, &engineMatrix[0][0]);

	engineMatrix = joint->GetMatrix0() * engineMatrix;
	dVector omega (omega0 - omega1);

	dFloat speed = (omega % engineMatrix.m_front) * m_info.m_crownGearRatio * 9.55f;
	return speed;
}

void CustomVehicleController::BodyPartEngine::ApplyTorque(dFloat torqueMag)
{
	dMatrix matrix;

	EngineJoint* const joint = (EngineJoint*)GetJoint();
	NewtonBody* const engine = joint->GetBody0();
	NewtonBodyGetMatrix(engine, &matrix[0][0]);
	matrix = joint->GetMatrix0() * matrix;

	m_norminalTorque = dAbs(torqueMag);
	dVector torque(matrix.m_front.Scale(m_norminalTorque));
	NewtonBodyAddTorque(engine, &torque[0]);
}

void CustomVehicleController::BodyPartEngine::Update(dFloat timestep, dFloat gasVal)
{
	dMatrix engineMatrix;
	dVector engineOmega;

	EngineJoint* const joint = (EngineJoint*)GetJoint();
	NewtonBody* const engine = joint->GetBody0();

	m_differential0.m_leftGear->SetGain (m_info.m_gearRatios[m_currentGear]);
	m_differential0.m_rightGear->SetGain (m_info.m_gearRatios[m_currentGear]);
	if (m_differential1.m_leftGear) {
		m_differential1.m_leftGear->SetGain(m_info.m_gearRatios[m_currentGear]);
		m_differential1.m_rightGear->SetGain(m_info.m_gearRatios[m_currentGear]);
	}

	NewtonBodyGetOmega(engine, &engineOmega[0]);
	NewtonBodyGetMatrix(engine, &engineMatrix[0][0]);
	engineMatrix = joint->GetMatrix0() * engineMatrix;

	dFloat W = engineOmega % engineMatrix.m_front;
	dFloat D0 = m_idleViscousFriction2 * (W * W * W * W);
	dFloat T = m_torqueRPMCurve.GetValue(W) * gasVal;
	W = dMin (W, m_torqueRPMCurve.m_nodes[1].m_param);
	dFloat D1 = m_idleViscousFriction * W * W;

	m_norminalTorque = T;
	dVector torque (engineMatrix.m_front.Scale (m_norminalTorque - D0 - D1));
	NewtonBodyAddTorque(engine, &torque[0]);
//dVector xxx (engineMatrix.m_up.Scale(5.0));
//NewtonBodySetOmega(engine, &xxx[0]);
}

void CustomVehicleController::BodyPartEngine::UpdateAutomaticGearBox(dFloat timestep)
{
m_info.m_gearsCount = 4;

	m_gearTimer --;
	if (m_gearTimer < 0) {
		dVector omega;
		dMatrix matrix;

		EngineJoint* const joint = (EngineJoint*)GetJoint();
		NewtonBody* const engine = joint->GetBody0();
		NewtonBodyGetOmega(engine, &omega[0]);
		NewtonBodyGetMatrix(engine, &matrix[0][0]);
		omega = matrix.UnrotateVector(omega);
		dFloat W = omega.m_x * m_info.m_crownGearRatio;
	
		switch (m_currentGear) 
		{
			case D_VEHICLE_NEUTRAL_GEAR:
			{
				dAssert (0);
				break;
			}

			case D_VEHICLE_REVERSE_GEAR:
			{
				  dAssert(0);
				  break;
			}

			default:
			{
				if (W > m_info.m_rpmAtPeakHorsePower) {
					if (m_currentGear < (m_info.m_gearsCount - 1)) {
						SetGear(m_currentGear + 1);
					}
				} else if (W < m_info.m_rpmAtPeakTorque) {
					if (m_currentGear > D_VEHICLE_FIRST_GEAR) {
						SetGear(m_currentGear - 1);
					}
				}
			}
		}
	}
}

int CustomVehicleController::BodyPartEngine::GetNeutralGear() const
{
	return D_VEHICLE_NEUTRAL_GEAR;
}

int CustomVehicleController::BodyPartEngine::GetReverseGear() const
{
	return D_VEHICLE_REVERSE_GEAR;
}

int CustomVehicleController::BodyPartEngine::GetGear() const
{
	return m_currentGear;
}

void CustomVehicleController::BodyPartEngine::SetGear(int gear)
{
	m_gearTimer = 30;
	dFloat oldGain = m_info.m_gearRatios[m_currentGear];
	m_currentGear = dClamp(gear, 0, m_info.m_gearsCount);

	dVector omega;
	dMatrix matrix;

	EngineJoint* const joint = (EngineJoint*)GetJoint();
	NewtonBody* const engine = joint->GetBody0();
	NewtonBodyGetOmega(engine, &omega[0]);
	NewtonBodyGetMatrix(engine, &matrix[0][0]);
	omega = matrix.UnrotateVector(omega);
	omega.m_x *= m_info.m_gearRatios[m_currentGear] / oldGain;
	omega = matrix.RotateVector(omega);
	NewtonBodySetOmega(engine, &omega[0]);
}
*/

CustomVehicleController::SteeringController::SteeringController (CustomVehicleController* const controller, dFloat maxAngle)
	:Controller(controller)
	,m_maxAngle(dAbs (maxAngle))
	,m_akermanWheelBaseWidth(0.0f)
	,m_akermanAxelSeparation(0.0f)
{
}

void CustomVehicleController::SteeringController::CalculateAkermanParameters(
	const BodyPartTire* const rearLeftTire, const BodyPartTire* const rearRightTire,
	const BodyPartTire* const frontLeftTire, const BodyPartTire* const frontRightTire)
{
/*
	const dMatrix& leftRearMatrix = rearLeftTire->GetLocalMatrix();
	const dMatrix& rightRearMatrix = rearRightTire->GetLocalMatrix();
	dVector rearDist(rightRearMatrix.m_posit - leftRearMatrix.m_posit);
	m_akermanWheelBaseWidth = (rearDist % leftRearMatrix.m_front) * 0.5f;

	const dMatrix& frontLeftTireMatrix = frontLeftTire->GetLocalMatrix();
	dVector akermanAxelSeparation(frontLeftTireMatrix.m_posit - leftRearMatrix.m_posit);
	m_akermanAxelSeparation = dAbs(akermanAxelSeparation % frontLeftTireMatrix.m_right);
*/
}

void CustomVehicleController::SteeringController::Update(dFloat timestep)
{
	dFloat angle = m_maxAngle * m_param;
	if ((m_akermanWheelBaseWidth == 0.0f) || (dAbs(angle) < (2.0f * 3.141592f / 180.0f))) {
		for (dList<BodyPartTire*>::dListNode* node = m_tires.GetFirst(); node; node = node->GetNext()) {
			BodyPartTire& tire = *node->GetInfo();
			tire.SetSteerAngle(angle);
		}
	} else {
		dAssert (0);
/*
		dAssert(dAbs(angle) >= (2.0f * 3.141592f / 180.0f));
		dFloat posit = m_akermanAxelSeparation / dTan(dAbs(angle));
		dFloat sign = dSign(angle);
		dFloat leftAngle = sign * dAtan2(m_akermanAxelSeparation, posit + m_akermanWheelBaseWidth);
		dFloat righAngle = sign * dAtan2(m_akermanAxelSeparation, posit - m_akermanWheelBaseWidth);
		for (dList<BodyPartTire*>::dListNode* node = m_steeringTires.GetFirst(); node; node = node->GetNext()) {
			BodyPartTire& tire = *node->GetInfo();
			tire.SetSteerAngle ((sign * tire.m_data.m_l > 0.0f) ? leftAngle : righAngle);
		}
*/	
	}
}

void CustomVehicleController::SteeringController::AddTire (CustomVehicleController::BodyPartTire* const tire)
{
	m_tires.Append(tire);
}

CustomVehicleController::BrakeController::BrakeController(CustomVehicleController* const controller, dFloat maxBrakeTorque)
	:Controller(controller)
	,m_maxTorque(maxBrakeTorque)
{
}

void CustomVehicleController::BrakeController::AddTire(BodyPartTire* const tire)
{
	m_tires.Append(tire);
}

void CustomVehicleController::BrakeController::Update(dFloat timestep)
{
	dFloat torque = m_maxTorque * m_param;
	for (dList<BodyPartTire*>::dListNode* node = m_tires.GetFirst(); node; node = node->GetNext()) {
		BodyPartTire& tire = *node->GetInfo();
		tire.SetBrakeTorque (torque);
	}
}


CustomVehicleController::EngineController::Differential::Differential (EngineController* const controller)
{
	dAssert (0);
}
	

CustomVehicleController::EngineController::Differential::Differential(EngineController* const controller, const DifferentialAxel& axel0)
{
//	dAssert (0);
}

void CustomVehicleController::EngineController::Differential::Update (dFloat timestep)
{
	dAssert (0);
}

CustomVehicleController::EngineController::Differential::~Differential()
{
//	dAssert (0);
}

CustomVehicleController::EngineController::DualDifferential::DualDifferential (EngineController* const controller, const DifferentialAxel& axel0, const DifferentialAxel& axel1)
	:Differential (controller)
{
	dAssert (0);
}

CustomVehicleController::EngineController::DualDifferential::~DualDifferential()
{
	dAssert (0);
}

void CustomVehicleController::EngineController::DualDifferential::Update (dFloat timestep)
{
	dAssert (0);
}


//CustomVehicleController::EngineController::EngineController (CustomVehicleController* const controller, BodyPartEngine* const engine)
CustomVehicleController::EngineController::EngineController (CustomVehicleController* const controller, const Info& info, const DifferentialAxel& axel0, const DifferentialAxel& axel1)
	:Controller(controller)
	,m_info(m_info)
//	,m_engine(engine)
	,m_automaticTransmissionMode(true)
{
//	m_parent = &controller->m_chassis;
//	m_controller = controller;


	//NewtonWorld* const world = ((CustomVehicleControllerManager*)m_controller->GetManager())->GetWorld();
	//NewtonCollision* const collision = NewtonCreateNull(world);
	//NewtonCollision* const collision = NewtonCreateSphere(world, 0.1f, 0, NULL);
	//NewtonCollision* const collision = NewtonCreateCylinder(world, 0.5f, 0.5f, 0, NULL);
	//dMatrix engineMatrix (CalculateEngineMatrix());
	//m_body = NewtonCreateDynamicBody(world, collision, &engineMatrix[0][0]);
	//NewtonDestroyCollision(collision);
	//EngineJoint* const engineJoint = new EngineJoint(m_body, m_parent->GetBody());
	//m_joint = engineJoint;

	if (axel1.m_leftTire) {
		dAssert (0);
		dAssert (axel0.m_leftTire);
		dAssert (axel0.m_rightTire);
		dAssert (axel1.m_leftTire);
		dAssert (axel1.m_rightTire);
		m_differential = new DualDifferential (this, axel0, axel1);	
	} else if (axel0.m_leftTire) {
		dAssert (axel0.m_leftTire);
		dAssert (axel0.m_rightTire);
		m_differential = new Differential (this, axel0);
	}

	//m_differential0.m_leftGear = new DifferentialSpiderGearJoint (engineJoint, (WheelJoint*)m_differential0.m_leftTire->GetJoint());
	//m_differential0.m_rightGear = new DifferentialSpiderGearJoint (engineJoint, (WheelJoint*)m_differential0.m_rightTire->GetJoint());
	//if (m_differential1.m_leftTire) {
		//m_differential1.m_leftGear = new DifferentialSpiderGearJoint (engineJoint, (WheelJoint*)m_differential1.m_leftTire->GetJoint());
		//m_differential1.m_rightGear = new DifferentialSpiderGearJoint (engineJoint, (WheelJoint*)m_differential1.m_rightTire->GetJoint());
	//}
	//NewtonBodySetForceAndTorqueCallback(m_body, m_controller->m_forceAndTorque);
	//SetInfo (info);
}

CustomVehicleController::EngineController::~EngineController ()
{
	if (m_differential) {
		delete m_differential;
	}
}

void CustomVehicleController::EngineController::Update(dFloat timestep)
{
//	dAssert (0);
/*
	if (m_automaticTransmissionMode) {
		m_engine->UpdateAutomaticGearBox (timestep);
	}
	m_engine->Update (timestep, m_param);
*/
}

bool CustomVehicleController::EngineController::GetTransmissionMode() const
{
	return m_automaticTransmissionMode;
}

void CustomVehicleController::EngineController::SetTransmissionMode(bool mode)
{
	m_automaticTransmissionMode = mode;
}

int CustomVehicleController::EngineController::GetGear() const
{
	dAssert (0);
	return 0;
//	return m_engine->GetGear();
}

void CustomVehicleController::EngineController::SetGear(int gear)
{
	dAssert (0);
//	return m_engine->SetGear(gear);
}

int CustomVehicleController::EngineController::GetNeutralGear() const
{
	dAssert (0);
	return 0;
//	return m_engine->GetNeutralGear();
}

int CustomVehicleController::EngineController::GetReverseGear() const
{
	dAssert (0);
	return 0;
//	return m_engine->GetReverseGear();
}

dFloat CustomVehicleController::EngineController::GetRPM() const
{
	dAssert (0);
	return 0;
//	return m_engine->GetRPM();
}

dFloat CustomVehicleController::EngineController::GetRedLineRPM() const
{
	dAssert (0);
	return 0;
//	return m_engine->GetRedLineRPM();
}

dFloat CustomVehicleController::EngineController::GetSpeed() const
{
	dAssert (0);
	return 0;
//	return m_engine->GetSpeed();
}

/*
CustomVehicleController::ClutchController::ClutchController(CustomVehicleController* const controller, BodyPartEngine* const engine, dFloat maxClutchTorque)
	:Controller(controller)
	,m_engine(engine)
	,m_maxTorque(maxClutchTorque)
{
}

void CustomVehicleController::ClutchController::Update(dFloat timestep)
{
	dFloat torque = m_maxTorque * m_param;
	m_engine->m_differential0.m_leftGear->SetClutch(torque);
	m_engine->m_differential0.m_rightGear->SetClutch(torque);

	if (m_engine->m_differential1.m_leftGear) {
		m_engine->m_differential1.m_leftGear->SetClutch(torque);
		m_engine->m_differential1.m_rightGear->SetClutch(torque);
	}
}
*/

void CustomVehicleControllerManager::DrawSchematic (const CustomVehicleController* const controller, dFloat scale) const
{
	controller->DrawSchematic(scale);
}

void CustomVehicleControllerManager::DrawSchematicCallback (const CustomVehicleController* const controller, const char* const partName, dFloat value, int pointCount, const dVector* const lines) const
{
}

#if 0
void CustomVehicleController::SetDryRollingFrictionTorque (dFloat dryRollingFrictionTorque)
{
	m_chassisState.SetDryRollingFrictionTorque (dryRollingFrictionTorque);
}

dFloat CustomVehicleController::GetDryRollingFrictionTorque () const
{
	return m_chassisState.GetDryRollingFrictionTorque();
}

CustomVehicleControllerBodyStateContact* CustomVehicleController::GetContactBody (const NewtonBody* const body)
{
	for (int i = 0; i < m_externalContactStatesCount; i ++) {
		if (m_externalContactStates[i]->m_newtonBody == body) {
			return m_externalContactStates[i];
		}
	}

	dAssert (m_externalContactStatesPool.GetCount() < 32);
	if (!m_freeContactList) {
		m_freeContactList = m_externalContactStatesPool.Append();
	}
	CustomVehicleControllerBodyStateContact* const externalBody = &m_freeContactList->GetInfo();
	m_freeContactList = m_freeContactList->GetNext(); 
	externalBody->Init (this, body);
	m_externalContactStates[m_externalContactStatesCount] = externalBody;
	m_externalContactStatesCount ++;
	dAssert (m_externalContactStatesCount < int (sizeof (m_externalContactStates) / sizeof (m_externalContactStates[0])));

	return externalBody;
}
#endif


void CustomVehicleController::LinksTiresKinematically (int count, BodyPartTire** const tires)
{
//	dFloat radio0 = tires[0]->m_data.m_radio;
	for (int i = 1; i < count; i ++) {
//		CustomVehicleControllerEngineDifferencialJoint* const link = &m_tankTireLinks.Append()->GetInfo();
//		link->Init(this, tires[0], tires[i]);
//		link->m_radio0 = radio0;
//		link->m_radio1 = tires[i]->m_radio;
	}
}

void CustomVehicleController::DrawSchematic(dFloat scale) const
{
	dVector array[32];
	dMatrix projectionMatrix(dGetIdentityMatrix());
	projectionMatrix[0][0] = scale;
	projectionMatrix[1][1] = 0.0f;
	projectionMatrix[2][1] = -scale;
	projectionMatrix[2][2] = 0.0f;
	CustomVehicleControllerManager* const manager = (CustomVehicleControllerManager*)GetManager();

	dFloat Ixx;
	dFloat Iyy;
	dFloat Izz;
	dFloat mass;
	dVector com;
	dMatrix matrix;
	NewtonBody* const chassisBody = m_chassis.GetBody();

	float velocityScale = 0.125f;

	NewtonBodyGetCentreOfMass(chassisBody, &com[0]);
	NewtonBodyGetMatrix(chassisBody, &matrix[0][0]);
	matrix.m_posit = matrix.TransformVector(com);

	NewtonBodyGetMassMatrix(chassisBody, &mass, &Ixx, &Iyy, &Izz);
	dMatrix chassisMatrix(GetLocalFrame() * matrix);
	dMatrix worldToComMatrix(chassisMatrix.Inverse() * projectionMatrix);
	{
		// draw vehicle chassis
		dVector p0(D_CUSTOM_LARGE_VALUE, D_CUSTOM_LARGE_VALUE, D_CUSTOM_LARGE_VALUE, 0.0f);
		dVector p1(-D_CUSTOM_LARGE_VALUE, -D_CUSTOM_LARGE_VALUE, -D_CUSTOM_LARGE_VALUE, 0.0f);

		//for (dList<CustomVehicleControllerBodyStateTire>::dListNode* node = m_tireList.GetFirst(); node; node = node->GetNext()) {
		for (dList<BodyPartTire>::dListNode* node = m_tireList.GetFirst(); node; node = node->GetNext()) {
			BodyPartTire* const tire = &node->GetInfo();
			NewtonBody* const tireBody = tire->GetBody();
			dMatrix matrix;
			NewtonBodyGetMatrix(tireBody, &matrix[0][0]);
			//dMatrix matrix (tire->CalculateSteeringMatrix() * m_chassisState.GetMatrix());
			dVector p(worldToComMatrix.TransformVector(matrix.m_posit));
			p0 = dVector(dMin(p.m_x, p0.m_x), dMin(p.m_y, p0.m_y), dMin(p.m_z, p0.m_z), 1.0f);
			p1 = dVector(dMax(p.m_x, p1.m_x), dMax(p.m_y, p1.m_y), dMax(p.m_z, p1.m_z), 1.0f);
		}

		array[0] = dVector(p0.m_x, p0.m_y, p0.m_z, 1.0f);
		array[1] = dVector(p1.m_x, p0.m_y, p0.m_z, 1.0f);
		array[2] = dVector(p1.m_x, p1.m_y, p0.m_z, 1.0f);
		array[3] = dVector(p0.m_x, p1.m_y, p0.m_z, 1.0f);
		manager->DrawSchematicCallback(this, "chassis", 0, 4, array);
	}

	{
		// draw vehicle tires
		for (dList<BodyPartTire>::dListNode* node = m_tireList.GetFirst(); node; node = node->GetNext()) {
			BodyPartTire* const tire = &node->GetInfo();
			dFloat width = tire->m_data.m_width * 0.5f;
			dFloat radio = tire->m_data.m_radio;

			NewtonBody* const tireBody = tire->GetBody();
			NewtonBodyGetMatrix(tireBody, &matrix[0][0]);
			matrix.m_up = chassisMatrix.m_up;
			matrix.m_right = matrix.m_front * matrix.m_up;

			array[0] = worldToComMatrix.TransformVector(matrix.TransformVector(dVector(width, 0.0f, radio, 0.0f)));
			array[1] = worldToComMatrix.TransformVector(matrix.TransformVector(dVector(width, 0.0f, -radio, 0.0f)));
			array[2] = worldToComMatrix.TransformVector(matrix.TransformVector(dVector(-width, 0.0f, -radio, 0.0f)));
			array[3] = worldToComMatrix.TransformVector(matrix.TransformVector(dVector(-width, 0.0f, radio, 0.0f)));
			manager->DrawSchematicCallback(this, "tire", 0, 4, array);
		}
	}

	{
		// draw vehicle velocity
		dVector veloc;
		dVector omega;

		NewtonBodyGetOmega(chassisBody, &omega[0]);
		NewtonBodyGetVelocity(chassisBody, &veloc[0]);

		dVector localVelocity(chassisMatrix.UnrotateVector(veloc));
		localVelocity.m_y = 0.0f;

		localVelocity = projectionMatrix.RotateVector(localVelocity);
		array[0] = dVector(0.0f, 0.0f, 0.0f, 0.0f);
		array[1] = localVelocity.Scale(velocityScale);
		manager->DrawSchematicCallback(this, "velocity", 0, 2, array);

		dVector localOmega(chassisMatrix.UnrotateVector(omega));
		array[0] = dVector(0.0f, 0.0f, 0.0f, 0.0f);
		array[1] = dVector(0.0f, localOmega.m_y * 10.0f, 0.0f, 0.0f);
		manager->DrawSchematicCallback(this, "omega", 0, 2, array);
	}

	{
		dFloat scale(2.0f / (mass * 10.0f));
		// draw vehicle forces
		for (dList<BodyPartTire>::dListNode* node = m_tireList.GetFirst(); node; node = node->GetNext()) {
			BodyPartTire* const tire = &node->GetInfo();
			//dMatrix matrix (tire->CalculateSteeringMatrix() * m_chassisState.GetMatrix());
			dMatrix matrix;
			NewtonBody* const tireBody = tire->GetBody();
			NewtonBodyGetMatrix(tireBody, &matrix[0][0]);
			matrix.m_up = chassisMatrix.m_up;
			matrix.m_right = matrix.m_front * matrix.m_up;

			//dTrace (("(%f %f %f) (%f %f %f)\n", p0.m_x, p0.m_y, p0.m_z, matrix.m_posit.m_x, matrix.m_posit.m_y, matrix.m_posit.m_z ));
			dVector origin(worldToComMatrix.TransformVector(matrix.m_posit));

			dVector lateralForce(chassisMatrix.UnrotateVector(GetTireLateralForce(tire)));
			lateralForce = lateralForce.Scale(-scale);
			lateralForce = projectionMatrix.RotateVector(lateralForce);

			array[0] = origin;
			array[1] = origin + lateralForce;
			manager->DrawSchematicCallback(this, "lateralForce", 0, 2, array);

			dVector longitudinalForce(chassisMatrix.UnrotateVector(GetTireLongitudinalForce(tire)));
			longitudinalForce = longitudinalForce.Scale(-scale);
			longitudinalForce = projectionMatrix.RotateVector(longitudinalForce);

			array[0] = origin;
			array[1] = origin + longitudinalForce;
			manager->DrawSchematicCallback(this, "longitudinalForce", 0, 2, array);

			dVector veloc;
			NewtonBodyGetVelocity(tireBody, &veloc[0]);
			veloc = chassisMatrix.UnrotateVector(veloc);
			veloc.m_y = 0.0f;
			veloc = projectionMatrix.RotateVector(veloc);
			array[0] = origin;
			array[1] = origin + veloc.Scale(velocityScale);
			manager->DrawSchematicCallback(this, "tireVelocity", 0, 2, array);
		}
	}
}

CustomVehicleControllerManager::CustomVehicleControllerManager(NewtonWorld* const world, int materialCount, int* const materialsList)
	:CustomControllerManager<CustomVehicleController> (world, VEHICLE_PLUGIN_NAME)
	,m_tireMaterial(NewtonMaterialCreateGroupID(world))
{
	// create the normalized size tire shape
	m_tireShapeTemplate = NewtonCreateChamferCylinder(world, 0.5f, 1.0f, 0, NULL);
	m_tireShapeTemplateData = NewtonCollisionDataPointer(m_tireShapeTemplate);

	// create a tire material and associate with the material the vehicle new to collide 
	for (int i = 0; i < materialCount; i ++) {
		NewtonMaterialSetCallbackUserData (world, m_tireMaterial, materialsList[i], this);
		if (m_tireMaterial != materialsList[i]) {
			NewtonMaterialSetContactGenerationCallback (world, m_tireMaterial, materialsList[i], OnContactGeneration);
		}
		NewtonMaterialSetCollisionCallback(world, m_tireMaterial, materialsList[i], OnTireAABBOverlap, OnTireContactsProcess);
	}
}

CustomVehicleControllerManager::~CustomVehicleControllerManager()
{
	NewtonDestroyCollision(m_tireShapeTemplate);
}

void CustomVehicleControllerManager::DestroyController(CustomVehicleController* const controller)
{
	controller->Cleanup();
	CustomControllerManager<CustomVehicleController>::DestroyController(controller);
}

int CustomVehicleControllerManager::GetTireMaterial() const
{
	return m_tireMaterial;
}

CustomVehicleController* CustomVehicleControllerManager::CreateVehicle(NewtonBody* const body, const dMatrix& vehicleFrame, NewtonApplyForceAndTorque forceAndTorque, void* const userData)
{
	dAssert (0);
//	CustomVehicleController* const controller = CreateController();
//	controller->Init(body, vehicleFrame, gravityVector);
//	return controller;
return NULL;	
}

CustomVehicleController* CustomVehicleControllerManager::CreateVehicle(NewtonCollision* const chassisShape, const dMatrix& vehicleFrame, dFloat mass, NewtonApplyForceAndTorque forceAndTorque, void* const userData)
{
	CustomVehicleController* const controller = CreateController();
	controller->Init(chassisShape, vehicleFrame, mass, forceAndTorque, userData);
	return controller;
}

void CustomVehicleController::Init(NewtonCollision* const chassisShape, const dMatrix& vehicleFrame, dFloat mass, NewtonApplyForceAndTorque forceAndTorque, void* const userData)
{
	CustomVehicleControllerManager* const manager = (CustomVehicleControllerManager*)GetManager();
	NewtonWorld* const world = manager->GetWorld();

	// create a body and an call the low level init function
	dMatrix locationMatrix(dGetIdentityMatrix());
	NewtonBody* const body = NewtonCreateDynamicBody(world, chassisShape, &locationMatrix[0][0]);

	// set vehicle mass, inertia and center of mass
	NewtonBodySetMassProperties(body, mass, chassisShape);

	// initialize 
	Init(body, vehicleFrame, forceAndTorque, userData);
}

void CustomVehicleController::Init(NewtonBody* const body, const dMatrix& vehicleFrame, NewtonApplyForceAndTorque forceAndTorque, void* const userData)
{

	m_body = body;
	m_finalized = false;
	m_localFrame = vehicleFrame;
	m_localFrame.m_posit = dVector (0.0f, 0.0f, 0.0f, 1.0f);
	m_forceAndTorque = forceAndTorque;
	
	CustomVehicleControllerManager* const manager = (CustomVehicleControllerManager*)GetManager();
	NewtonWorld* const world = manager->GetWorld();

	// set linear and angular drag to zero
	dVector drag(0.0f, 0.0f, 0.0f, 0.0f);
	NewtonBodySetLinearDamping(m_body, 0);
	NewtonBodySetAngularDamping(m_body, &drag[0]);

	// set the standard force and torque call back
	NewtonBodySetForceAndTorqueCallback(body, m_forceAndTorque);

	m_contactFilter = new BodyPartTire::FrictionModel(this);

//	SetDryRollingFrictionTorque(100.0f / 4.0f);
	// assume gravity is 10.0f, and a speed of 60 miles/hours
	SetAerodynamicsDownforceCoefficient(2.0f * 10.0f, 60.0f * 0.447f);

//	m_engine = NULL;
//	m_cluthControl = NULL;
	m_brakesControl = NULL;
	m_engineControl = NULL;
	m_steeringControl = NULL;
	m_handBrakesControl = NULL;
	
	m_collisionAggregate = NewtonCollisionAggregateCreate(world);
	NewtonCollisionAggregateSetSelfCollision (m_collisionAggregate, 0);
	NewtonCollisionAggregateAddBody (m_collisionAggregate, m_body);

	m_skeleton = NewtonSkeletonContainerCreate(world, m_body, NULL);

	m_chassis.Init(this, userData);
	m_bodyPartsList.Append(&m_chassis);

#ifdef D_PLOT_ENGINE_CURVE 
	file_xxx = fopen("vehiceLog.csv", "wb");
	fprintf (file_xxx, "eng_rpm, eng_torque, eng_nominalTorque,\n");
#endif
}

void CustomVehicleController::Cleanup()
{
//	SetClutch(NULL);
	SetBrakes(NULL);
	SetEngine(NULL);
	SetSteering(NULL);
	SetHandBrakes(NULL);
	SetContactFilter(NULL);
/*
	if (m_engine) {
		dAssert (0);
		delete m_engine;
	}
*/
}

const CustomVehicleController::BodyPart* CustomVehicleController::GetChassis() const
{
	return &m_chassis;
}

const dMatrix& CustomVehicleController::GetLocalFrame() const
{
	return m_localFrame;
}

dMatrix CustomVehicleController::GetTransform() const
{
	dMatrix matrix;
	NewtonBodyGetMatrix (m_chassis.GetBody(), &matrix[0][0]);
	return matrix;
}

void CustomVehicleController::SetTransform(const dMatrix& matrix)
{
	NewtonBodySetMatrixRecursive (m_chassis.GetBody(), &matrix[0][0]);
}


CustomVehicleController::EngineController* CustomVehicleController::GetEngine() const
{
	return m_engineControl;
}

/*
CustomVehicleController::ClutchController* CustomVehicleController::GetClutch() const
{
	return m_cluthControl;
}
*/

CustomVehicleController::SteeringController* CustomVehicleController::GetSteering() const
{
	return m_steeringControl;
}

CustomVehicleController::BrakeController* CustomVehicleController::GetBrakes() const
{
	return m_brakesControl;
}

CustomVehicleController::BrakeController* CustomVehicleController::GetHandBrakes() const
{
	return m_handBrakesControl;
}

void CustomVehicleController::SetEngine(EngineController* const engineControl)
{
	if (m_engineControl) {
		delete m_engineControl;
	}
	m_engineControl = engineControl;
}

/*
void CustomVehicleController::SetClutch(ClutchController* const cluth)
{
	if (m_cluthControl) {
		delete m_cluthControl;
	}
	m_cluthControl = cluth;
}
*/

void CustomVehicleController::SetHandBrakes(BrakeController* const handBrakes)
{
	if (m_handBrakesControl) {
		delete m_handBrakesControl;
	}
	m_handBrakesControl = handBrakes;
}

void CustomVehicleController::SetBrakes(BrakeController* const brakes)
{
	if (m_brakesControl) {
		delete m_brakesControl;
	}
	m_brakesControl = brakes;
}


void CustomVehicleController::SetSteering(SteeringController* const steering)
{
	if (m_steeringControl) {
		delete m_steeringControl;
	}
	m_steeringControl = steering;
}

void CustomVehicleController::SetContactFilter(BodyPartTire::FrictionModel* const filter)
{
	if (m_contactFilter) {
		delete m_contactFilter;
	}
	m_contactFilter = filter;
}

dList<CustomVehicleController::BodyPartTire>::dListNode* CustomVehicleController::GetFirstTire() const
{
	return m_tireList.GetFirst();
}

dList<CustomVehicleController::BodyPartTire>::dListNode* CustomVehicleController::GetNextTire(dList<CustomVehicleController::BodyPartTire>::dListNode* const tireNode) const
{
	return tireNode->GetNext();
}

dList<CustomVehicleController::BodyPart*>::dListNode* CustomVehicleController::GetFirstBodyPart() const
{
	return m_bodyPartsList.GetFirst();
}

dList<CustomVehicleController::BodyPart*>::dListNode* CustomVehicleController::GetNextBodyPart(dList<BodyPart*>::dListNode* const part) const
{
	return part->GetNext();
}

void CustomVehicleController::SetCenterOfGravity(const dVector& comRelativeToGeomtriCenter)
{
	NewtonBodySetCentreOfMass(m_body, &comRelativeToGeomtriCenter[0]);
}


void CustomVehicleController::Finalize()
{

NewtonBodySetMassMatrix(GetBody(), 0.0f, 0.0f, 0.0f, 0.0f);

	m_finalized = true;
	NewtonSkeletonContainerFinalize(m_skeleton);

/*
	dWeightDistibutionSolver solver;
	dFloat64 unitAccel[256];
	dFloat64 sprungMass[256];

	int count = 0;
	dVector dir (0.0f, 1.0f, 0.0f, 0.0f);
	
	dMatrix matrix;
	dVector com;
	dVector invInertia;
	dFloat invMass;
	NewtonBodyGetMatrix(m_body, &matrix[0][0]);
	NewtonBodyGetCentreOfMass(m_body, &com[0]);
	NewtonBodyGetInvMass(m_body, &invMass, &invInertia[0], &invInertia[1], &invInertia[2]);
	matrix = matrix.Inverse();

	for (dList<BodyPartTire>::dListNode* node = m_tireList.GetFirst(); node; node = node->GetNext()) {
		BodyPartTire* const tire = &node->GetInfo();
		
		dMatrix tireMatrix;
		NewtonBodyGetMatrix(tire->GetBody(), &tireMatrix[0][0]);
		tireMatrix = tireMatrix * matrix;

		dVector posit  (tireMatrix.m_posit - com);  

		dComplentaritySolver::dJacobian &jacobian0 = solver.m_jacobians[count];
		dComplentaritySolver::dJacobian &invMassJacobian0 = solver.m_invMassJacobians[count];
		jacobian0.m_linear = dir;
		jacobian0.m_angular = posit * dir;
		jacobian0.m_angular.m_w = 0.0f;

		invMassJacobian0.m_linear = jacobian0.m_linear.Scale(invMass);
		invMassJacobian0.m_angular = jacobian0.m_angular.CompProduct(invInertia);

		dFloat diagnal = jacobian0.m_linear % invMassJacobian0.m_linear + jacobian0.m_angular % invMassJacobian0.m_angular;
		solver.m_diagRegularizer[count] = diagnal * 0.005f;
		solver.m_invDiag[count] = 1.0f / (diagnal + solver.m_diagRegularizer[count]);

		unitAccel[count] = 1.0f;
		sprungMass[count] = 0.0f;
		count ++;
	}

	if (count) {
		solver.m_count = count;
		solver.Solve (count, 1.0e-6f, sprungMass, unitAccel);
	}

	int index = 0;
	for (dList<BodyPartTire>::dListNode* node = m_tireList.GetFirst(); node; node = node->GetNext()) {
		BodyPartTire* const tire = &node->GetInfo();
		WheelJoint* const tireJoint = (WheelJoint*) tire->GetJoint();
		tireJoint->m_restSprunMass = dFloat (5.0f * dFloor (sprungMass[index] / 5.0f + 0.5f));
		index ++;
	}
*/
}

bool CustomVehicleController::ControlStateChanged() const
{
	bool inputChanged = (m_steeringControl && m_steeringControl->ParamChanged());
	inputChanged = inputChanged || (m_engineControl && m_engineControl ->ParamChanged());
	inputChanged = inputChanged || (m_brakesControl && m_brakesControl->ParamChanged());
	inputChanged = inputChanged || (m_handBrakesControl && m_handBrakesControl->ParamChanged());
	return inputChanged;
}

CustomVehicleController::BodyPartTire* CustomVehicleController::AddTire(const BodyPartTire::Info& tireInfo)
{
	dList<BodyPartTire>::dListNode* const tireNode = m_tireList.Append();
	BodyPartTire& tire = tireNode->GetInfo();

	// calculate the tire matrix location,
	dMatrix matrix;
	NewtonBodyGetMatrix(m_body, &matrix[0][0]);
	matrix = m_localFrame * matrix;
	matrix.m_posit = matrix.TransformVector (tireInfo.m_location);
	matrix.m_posit.m_w = 1.0f;

	tire.Init(&m_chassis, matrix, tireInfo);
	tire.m_index = m_tireList.GetCount() - 1;

	m_bodyPartsList.Append(&tire);
	NewtonCollisionAggregateAddBody (m_collisionAggregate, tire.GetBody());
	NewtonSkeletonContainerAttachBone (m_skeleton, tire.GetBody(), m_chassis.GetBody());
	return &tireNode->GetInfo();
}

/*
CustomVehicleController::BodyPartEngine* CustomVehicleController::GetEngineBodyPart() const
{
	return m_engine;
}

void CustomVehicleController::AddEngineBodyPart (BodyPartEngine* const engine)
{
	if (m_engine) {
		delete m_engine;
	}

	m_engine = engine;
	NewtonCollisionAggregateAddBody(m_collisionAggregate, m_engine->GetBody());
	NewtonSkeletonContainerAttachBone(m_skeleton, m_engine->GetBody(), m_chassis.GetBody());
}
*/

dVector CustomVehicleController::GetTireNormalForce(const BodyPartTire* const tire) const
{
	WheelJoint* const joint = (WheelJoint*) tire->GetJoint();
	dFloat force = joint->GetTireLoad();
	return dVector (0.0f, force, 0.0f, 0.0f);
}

dVector CustomVehicleController::GetTireLateralForce(const BodyPartTire* const tire) const
{
	WheelJoint* const joint = (WheelJoint*)tire->GetJoint();
	return joint->GetLateralForce();
}

dVector CustomVehicleController::GetTireLongitudinalForce(const BodyPartTire* const tire) const
{
	WheelJoint* const joint = (WheelJoint*)tire->GetJoint();
	return joint->GetLongitudinalForce();
}

dFloat CustomVehicleController::GetAerodynamicsDowforceCoeficient() const
{
	return m_chassis.m_aerodynamicsDownForceCoefficient;
}

void CustomVehicleController::SetAerodynamicsDownforceCoefficient(dFloat maxDownforceInGravity, dFloat topSpeed)
{
	dFloat Ixx;
	dFloat Iyy;
	dFloat Izz;
	dFloat mass;
	NewtonBody* const body = GetBody();
	NewtonBodyGetMassMatrix(body, &mass, &Ixx, &Iyy, &Izz);
	m_chassis.m_aerodynamicsDownForceCoefficient = mass * maxDownforceInGravity / (topSpeed * topSpeed);
}

int CustomVehicleControllerManager::OnTireAABBOverlap(const NewtonMaterial* const material, const NewtonBody* const body0, const NewtonBody* const body1, int threadIndex)
{
	CustomVehicleControllerManager* const manager = (CustomVehicleControllerManager*)NewtonMaterialGetMaterialPairUserData(material);

	const NewtonCollision* const collision0 = NewtonBodyGetCollision(body0);
	const void* const data0 = NewtonCollisionDataPointer(collision0);
	if (data0 == manager->m_tireShapeTemplateData) {
		const NewtonBody* const otherBody = body1;
		const CustomVehicleController::BodyPartTire* const tire = (CustomVehicleController::BodyPartTire*) NewtonCollisionGetUserData1(collision0);
		dAssert(tire->GetParent()->GetBody() != otherBody);
		return manager->OnTireAABBOverlap(material, tire, otherBody);
	} 
	const NewtonCollision* const collision1 = NewtonBodyGetCollision(body1);
	dAssert (NewtonCollisionDataPointer(collision1) == manager->m_tireShapeTemplateData) ;
	const NewtonBody* const otherBody = body0;
	const CustomVehicleController::BodyPartTire* const tire = (CustomVehicleController::BodyPartTire*) NewtonCollisionGetUserData1(collision1);
	dAssert(tire->GetParent()->GetBody() != otherBody);
	return manager->OnTireAABBOverlap(material, tire, otherBody);
}

int CustomVehicleControllerManager::OnTireAABBOverlap(const NewtonMaterial* const material, const CustomVehicleController::BodyPartTire* const tire, const NewtonBody* const otherBody) const
{
	for (int i = 0; i < tire->m_collidingCount; i ++) {
		if (otherBody == tire->m_info[i].m_hitBody) {
			return true;
		}
	}
	return false;
}

int CustomVehicleControllerManager::OnContactGeneration (const NewtonMaterial* const material, const NewtonBody* const body0, const NewtonCollision* const collision0, const NewtonBody* const body1, const NewtonCollision* const collision1, NewtonUserContactPoint* const contactBuffer, int maxCount, int threadIndex)
{
	CustomVehicleControllerManager* const manager = (CustomVehicleControllerManager*) NewtonMaterialGetMaterialPairUserData(material);
	const void* const data0 = NewtonCollisionDataPointer(collision0);
	const void* const data1 = NewtonCollisionDataPointer(collision1);
	dAssert ((data0 == manager->m_tireShapeTemplateData) || (data1 == manager->m_tireShapeTemplateData));
	dAssert (!((data0 == manager->m_tireShapeTemplateData) && (data1 == manager->m_tireShapeTemplateData)));

	if (data0 == manager->m_tireShapeTemplateData) {
		const NewtonBody* const otherBody = body1;
		const NewtonCollision* const tireCollision = collision0;
		const NewtonCollision* const otherCollision = collision1;
		const CustomVehicleController::BodyPartTire* const tire = (CustomVehicleController::BodyPartTire*) NewtonCollisionGetUserData1(tireCollision);
		dAssert (tire->GetBody() == body0);
		return manager->OnContactGeneration (tire, otherBody, otherCollision, contactBuffer, maxCount, threadIndex);
	} 
	dAssert (data1 == manager->m_tireShapeTemplateData);
	const NewtonBody* const otherBody = body0;
	const NewtonCollision* const tireCollision = collision1;
	const NewtonCollision* const otherCollision = collision0;
	const CustomVehicleController::BodyPartTire* const tire = (CustomVehicleController::BodyPartTire*) NewtonCollisionGetUserData1(tireCollision);
	dAssert (tire->GetBody() == body1);
	int count = manager->OnContactGeneration(tire, otherBody, otherCollision, contactBuffer, maxCount, threadIndex);

	for (int i = 0; i < count; i ++) {	
		contactBuffer[i].m_normal[0] *= -1.0f;
		contactBuffer[i].m_normal[1] *= -1.0f;
		contactBuffer[i].m_normal[2] *= -1.0f;
		dSwap (contactBuffer[i].m_shapeId0, contactBuffer[i].m_shapeId1);
	}
	return count;
}

int CustomVehicleControllerManager::OnContactGeneration (const CustomVehicleController::BodyPartTire* const tire, const NewtonBody* const otherBody, const NewtonCollision* const othercollision, NewtonUserContactPoint* const contactBuffer, int maxCount, int threadIndex) const
{
	int count = 0;
	NewtonCollision* const collisionA = NewtonBodyGetCollision(tire->GetBody());
	dLong tireID = NewtonCollisionGetUserID(collisionA);
	for (int i = 0; i < tire->m_collidingCount; i ++) {
		if (otherBody == tire->m_info[i].m_hitBody) {
			contactBuffer[count].m_point[0] = tire->m_info[i].m_point[0];
			contactBuffer[count].m_point[1] = tire->m_info[i].m_point[1];
			contactBuffer[count].m_point[2] = tire->m_info[i].m_point[2];
			contactBuffer[count].m_point[3] = 1.0f;
			contactBuffer[count].m_normal[0] = tire->m_info[i].m_normal[0];
			contactBuffer[count].m_normal[1] = tire->m_info[i].m_normal[1];
			contactBuffer[count].m_normal[2] = tire->m_info[i].m_normal[2];
			contactBuffer[count].m_normal[3] = 0.0f;
			contactBuffer[count].m_penetration = 0.0f;
			contactBuffer[count].m_shapeId0 = tireID;
			contactBuffer[count].m_shapeId1 = tire->m_info[i].m_contactID;
			count ++;
		}				  
	}
	return count;
}

void CustomVehicleControllerManager::Collide(CustomVehicleController::BodyPartTire* const tire) const
{
	class TireFilter: public CustomControllerConvexCastPreFilter
	{
		public:
		TireFilter(const NewtonBody* const tire, const NewtonBody* const vehicle)
			:CustomControllerConvexCastPreFilter(tire)
			,m_vehicle(vehicle)
		{
		}

		unsigned Prefilter(const NewtonBody* const body, const NewtonCollision* const myCollision)
		{
			dAssert(body != m_me);
			return (body != m_vehicle) ? 1 : 0;
		}

		const NewtonBody* m_vehicle;
	};

	dMatrix tireMatrix;
	dMatrix chassisMatrix;

	const NewtonWorld* const world = GetWorld();
	const NewtonBody* const tireBody = tire->GetBody();
	const NewtonBody* const vehicleBody = tire->GetParent()->GetBody();
	CustomVehicleController* const controller = tire->GetController();

	NewtonBodyGetMatrix(tireBody, &tireMatrix[0][0]);
	NewtonBodyGetMatrix(vehicleBody, &chassisMatrix[0][0]);

	chassisMatrix = controller->m_localFrame * chassisMatrix;
	chassisMatrix.m_posit = chassisMatrix.TransformVector(tire->m_data.m_location);
	chassisMatrix.m_posit.m_w = 1.0f;

	dVector suspensionSpan (chassisMatrix.m_up.Scale(tire->m_data.m_suspesionlenght));

	dMatrix tireSweeptMatrix;
	tireSweeptMatrix.m_up = chassisMatrix.m_up;
	tireSweeptMatrix.m_right = tireMatrix.m_front * chassisMatrix.m_up;
	tireSweeptMatrix.m_right = tireSweeptMatrix.m_right.Scale(1.0f / dSqrt(tireSweeptMatrix.m_right % tireSweeptMatrix.m_right));
	tireSweeptMatrix.m_front = tireSweeptMatrix.m_up * tireSweeptMatrix.m_right;
	tireSweeptMatrix.m_posit = chassisMatrix.m_posit + suspensionSpan;

	NewtonCollision* const tireCollision = NewtonBodyGetCollision(tireBody);
	TireFilter filter(tireBody, vehicleBody);

	dFloat timeOfImpact;
	tire->m_collidingCount = NewtonWorldConvexCast (world, &tireSweeptMatrix[0][0], &chassisMatrix.m_posit[0], tireCollision, &timeOfImpact, &filter, CustomControllerConvexCastPreFilter::Prefilter, tire->m_info, sizeof (tire->m_info) / sizeof (tire->m_info[0]), 0);
	if (tire->m_collidingCount) {

		timeOfImpact = 1.0f - timeOfImpact;
		dFloat num = (tireMatrix.m_posit - chassisMatrix.m_posit) % suspensionSpan;
		dFloat tireParam = num / (tire->m_data.m_suspesionlenght * tire->m_data.m_suspesionlenght);

		if (tireParam <= timeOfImpact) {
			tireMatrix.m_posit = chassisMatrix.m_posit + chassisMatrix.m_up.Scale(timeOfImpact * tire->m_data.m_suspesionlenght);
			NewtonBodySetMatrixNoSleep(tireBody, &tireMatrix[0][0]);
		}
	}
}

void CustomVehicleControllerManager::OnTireContactsProcess (const NewtonJoint* const contactJoint, dFloat timestep, int threadIndex)
{
	void* const contact = NewtonContactJointGetFirstContact(contactJoint);
	dAssert (contact);
	NewtonMaterial* const material = NewtonContactGetMaterial(contact);
	CustomVehicleControllerManager* const manager = (CustomVehicleControllerManager*) NewtonMaterialGetMaterialPairUserData(material);

	const NewtonBody* const body0 = NewtonJointGetBody0(contactJoint);
	const NewtonBody* const body1 = NewtonJointGetBody1(contactJoint);
	const NewtonCollision* const collision0 = NewtonBodyGetCollision(body0);
	const void* const data0 = NewtonCollisionDataPointer(collision0);
	if (data0 == manager->m_tireShapeTemplateData) {
		const NewtonBody* const otherBody = body1;
		CustomVehicleController::BodyPartTire* const tire = (CustomVehicleController::BodyPartTire*) NewtonCollisionGetUserData1(collision0);
		dAssert(tire->GetParent()->GetBody() != otherBody);
		manager->OnTireContactsProcess(contactJoint, tire, otherBody, timestep);
	} else {
		const NewtonCollision* const collision1 = NewtonBodyGetCollision(body1);
		const void* const data1 = NewtonCollisionDataPointer(collision1);
		if (data1 == manager->m_tireShapeTemplateData) {
			const NewtonCollision* const collision1 = NewtonBodyGetCollision(body1);
			dAssert(NewtonCollisionDataPointer(collision1) == manager->m_tireShapeTemplateData);
			const NewtonBody* const otherBody = body0;
			CustomVehicleController::BodyPartTire* const tire = (CustomVehicleController::BodyPartTire*) NewtonCollisionGetUserData1(collision1);
			dAssert(tire->GetParent()->GetBody() != otherBody);
			manager->OnTireContactsProcess(contactJoint, tire, otherBody, timestep);
		}
	}
}

void CustomVehicleControllerManager::OnTireContactsProcess(const NewtonJoint* const contactJoint, CustomVehicleController::BodyPartTire* const tire, const NewtonBody* const otherBody, dFloat timestep)
{
	dAssert((tire->GetBody() == NewtonJointGetBody0(contactJoint)) || (tire->GetBody() == NewtonJointGetBody1(contactJoint)));

	dMatrix tireMatrix;
	dVector tireOmega;
	dVector tireVeloc;

	NewtonBody* const tireBody = tire->GetBody();
	const CustomVehicleController* const controller = tire->GetController();
	CustomVehicleController::WheelJoint* const tireJoint = (CustomVehicleController::WheelJoint*) tire->GetJoint();

	dAssert(tireJoint->GetBody0() == tireBody);
	NewtonBodyGetMatrix(tireBody, &tireMatrix[0][0]);
	tireMatrix = tireJoint->GetMatrix0() * tireMatrix;

	NewtonBodyGetOmega(tireBody, &tireOmega[0]);
	NewtonBodyGetVelocity(tireBody, &tireVeloc[0]);

	tire->m_lateralSlip = 0.0f;
	tire->m_aligningTorque = 0.0f;
	tire->m_longitudinalSlip = 0.0f;

	for (void* contact = NewtonContactJointGetFirstContact(contactJoint); contact; contact = NewtonContactJointGetNextContact(contactJoint, contact)) {
		dVector contactPoint;
		dVector contactNormal;
		NewtonMaterial* const material = NewtonContactGetMaterial(contact);
		NewtonMaterialGetContactPositionAndNormal(material, tireBody, &contactPoint[0], &contactNormal[0]);

		const dVector& lateralPin = tireMatrix.m_front;
		dVector tireAnglePin(contactNormal * lateralPin);
		dFloat pinMag2 = tireAnglePin % tireAnglePin;
		if (pinMag2 > 0.25f) {
			// brush rubber tire friction model
			// project the contact point to the surface of the collision shape
			dVector contactPatch(contactPoint - lateralPin.Scale((contactPoint - tireMatrix.m_posit) % lateralPin));
			dVector dp(contactPatch - tireMatrix.m_posit);
			dVector radius(dp.Scale(tire->m_data.m_radio / dSqrt(dp % dp)));

			dVector lateralContactDir;
			dVector longitudinalContactDir;
			NewtonMaterialContactRotateTangentDirections(material, &lateralPin[0]);
			NewtonMaterialGetContactTangentDirections(material, tireBody, &lateralContactDir[0], &longitudinalContactDir[0]);

			dFloat vy = tireVeloc % lateralPin;
			dFloat vx = tireVeloc % longitudinalContactDir;
			dFloat vw = -((tireOmega * radius) % longitudinalContactDir);

			if ((dAbs(vx) < (1.0f)) || (dAbs(vw) < 0.1f)) {
				// vehicle  moving at speed for which tire physics is undefined, simple do a kinematic motion
				NewtonMaterialSetContactFrictionCoef(material, 1.0f, 1.0f, 0);
				NewtonMaterialSetContactFrictionCoef(material, 1.0f, 1.0f, 1);
			} else {
				// calculating Brush tire model with longitudinal and lateral coupling 
				// for friction coupling according to Motor Vehicle dynamics by: Giancarlo Genta 
				// dFloat alphaTangent = vy / dAbs(vx);
				//dFloat k = (vw - vx) / vx;
				//dFloat phy_x0 = k / (1.0f + k);
				//dFloat phy_z0 = alphaTangent / (1.0f + k);

				// reduces to this, which may have a divide by zero locked, so I am cl;amping to some small value
				if (dAbs(vw) < 0.01f) {
					vw = 0.01f * dSign(vw);
				}
				dFloat phy_x = (vw - vx) / vw;
				dFloat phy_z = vy / dAbs (vw);

				dFloat f_x;
				dFloat f_z;
				dFloat moment;

				dVector tireLoadForce;
				NewtonMaterialGetContactForce(material, tireBody, &tireLoadForce.m_x);
				dFloat tireLoad = (tireLoadForce % contactNormal);

				controller->m_contactFilter->GetForces(tire, otherBody, material, tireLoad, phy_x, phy_z, f_x, f_z, moment);

//if ((tire->m_index == 0) || (tire->m_index == 1)) 
//{
//dTrace(("tire: %d  force (%f, %f %f), slip (%f %f)\n", tire->m_index, tireLoad, f_x, f_z, phy_x, phy_z));
//}

				dVector force (longitudinalContactDir.Scale (f_x) + lateralPin.Scale (f_z));
				dVector torque (radius * force);

				//NewtonBodyAddForce(tireBody, &force[0]);
				//NewtonBodyAddForce(tireBody, &torque[0]);

				NewtonMaterialSetContactTangentAcceleration (material, 0.0f, 0);
				NewtonMaterialSetContactTangentFriction(material, dAbs (f_z), 0);

				NewtonMaterialSetContactTangentAcceleration (material, 0.0f, 1);
				NewtonMaterialSetContactTangentFriction(material, dAbs (f_x), 1);
			}

		} else {
			NewtonMaterialSetContactFrictionCoef(material, 1.0f, 1.0f, 0);
			NewtonMaterialSetContactFrictionCoef(material, 1.0f, 1.0f, 1);
		}

		NewtonMaterialSetContactElasticity(material, 0.0f);
	}
}


void CustomVehicleController::PostUpdate(dFloat timestep, int threadIndex)
{
	if (m_finalized) {
#ifdef D_PLOT_ENGINE_CURVE 
		dFloat engineOmega = m_engine->GetRPM();
		dFloat tireTorque = m_engine->GetLeftSpiderGear()->m_tireTorque + m_engine->GetRightSpiderGear()->m_tireTorque;
		dFloat engineTorque = m_engine->GetLeftSpiderGear()->m_engineTorque + m_engine->GetRightSpiderGear()->m_engineTorque;
		fprintf(file_xxx, "%f, %f, %f,\n", engineOmega, engineTorque, m_engine->GetNominalTorque());
#endif
	}

//dTrace (("\n"));
}

void CustomVehicleController::PreUpdate(dFloat timestep, int threadIndex)
{
	if (m_finalized) {
		CustomVehicleControllerManager* const manager = (CustomVehicleControllerManager*) GetManager();
		for (dList<BodyPartTire>::dListNode* tireNode = m_tireList.GetFirst(); tireNode; tireNode = tireNode->GetNext()) {
			BodyPartTire& tire = tireNode->GetInfo();
			WheelJoint* const tireJoint = (WheelJoint*)tire.GetJoint();
			tireJoint->RemoveKinematicError(timestep);
			manager->Collide (&tire);
		}

		m_chassis.ApplyDownForce ();
		if (m_engineControl) {
//			BodyPartEngine* const engine = m_engineControl->m_engine;
//			EngineJoint* const joint = (EngineJoint*) engine->GetJoint();
//			joint->RemoveKinematicError();
			m_engineControl->Update(timestep);
		}

//		if (m_cluthControl) {
//			m_cluthControl->Update(timestep);
//		}

		if (m_steeringControl) {
			m_steeringControl->Update(timestep);
		}

		if (m_brakesControl) {
			m_brakesControl->Update(timestep);
		}

		if (m_handBrakesControl) {
			m_handBrakesControl->Update(timestep);
		}

		if (ControlStateChanged()) {
			NewtonBodySetSleepState(m_body, 0);
		}
	}
}
