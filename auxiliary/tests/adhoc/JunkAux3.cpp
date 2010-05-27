/* -------------------------------------------------------------------------- *
 *                      SimTK Core: SimTK Simbody(tm)                         *
 * -------------------------------------------------------------------------- *
 * This is part of the SimTK Core biosimulation toolkit originating from      *
 * Simbios, the NIH National Center for Physics-Based Simulation of           *
 * Biological Structures at Stanford, funded under the NIH Roadmap for        *
 * Medical Research, grant U54 GM072970. See https://simtk.org.               *
 *                                                                            *
 * Portions copyright (c) 2009 Stanford University and the Authors.           *
 * Authors: Michael Sherman                                                   *
 * Contributors:                                                              *
 *                                                                            *
 * Permission is hereby granted, free of charge, to any person obtaining a    *
 * copy of this software and associated documentation files (the "Software"), *
 * to deal in the Software without restriction, including without limitation  *
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,   *
 * and/or sell copies of the Software, and to permit persons to whom the      *
 * Software is furnished to do so, subject to the following conditions:       *
 *                                                                            *
 * The above copyright notice and this permission notice shall be included in *
 * all copies or substantial portions of the Software.                        *
 *                                                                            *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR *
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,   *
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL    *
 * THE AUTHORS, CONTRIBUTORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,    *
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR      *
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE  *
 * USE OR OTHER DEALINGS IN THE SOFTWARE.                                     *
 * -------------------------------------------------------------------------- */

/**@file
 * This is an outer block for simulating ??? in various ways with Simbody.
 * This is about testing Simbody, *not* studying ???!
 */

#include "SimTKsimbody.h"
#include "SimTKsimbody_aux.h" // requires VTK

#include <string>
#include <iostream>
#include <exception>
#include <ctime>

using std::cout;
using std::cin;
using std::endl;

using namespace SimTK;

const Real ReportInterval=0.01;
const Real RunTime=20;

class StateSaver : public PeriodicEventReporter {
public:
    StateSaver(const MultibodySystem&           system,
               const Constraint::ConstantSpeed& lock,
               const Integrator&                integ,
               Real reportInterval)
    :   PeriodicEventReporter(reportInterval), 
        m_system(system), m_lock(lock), m_integ(integ) {}

    ~StateSaver() {}

    void clear() {m_states.clear();}
    int getNumSavedStates() const {return m_states.size();}
    const State& getState(int n) const {return m_states[n];}

    void handleEvent(const State& s) const {
        const SimbodyMatterSubsystem& matter=m_system.getMatterSubsystem();
        const SpatialVec PG = matter.calcSystemMomentumAboutGroundOrigin(s);

        const bool isLocked = !m_lock.isDisabled(s);

        printf("%3d: %5g mom=%g,%g E=%g %s", m_integ.getNumStepsTaken(),
            s.getTime(),
            PG[0].norm(), PG[1].norm(), m_system.calcEnergy(s),
            isLocked?"LOCKED":"FREE");

        if (isLocked) {
            m_system.realize(s, Stage::Acceleration);
            printf(" lambda=%g", m_lock.getMultiplier(s));
            cout << " Triggers=" << s.getEventTriggers();
        }

        printf("\n");

        m_states.push_back(s);
    }
private:
    const MultibodySystem&              m_system;
    const Constraint::ConstantSpeed&    m_lock;
    const Integrator&                   m_integ;
    mutable Array_<State,int>           m_states;
};

class LockOn: public TriggeredEventHandler {
public:
    LockOn(const MultibodySystem& system,
        MobilizedBody& mobod, Real lockangle, // must be 1dof
        Constraint::ConstantSpeed& lock, 
        Constraint::ConstantAcceleration& dlock) 
    :   TriggeredEventHandler(Stage::Position), 
        mbs(system), mobod(mobod), lockangle(lockangle),
        lock(lock), dlock(dlock) 
    { 
	    //getTriggerInfo().setTriggerOnRisingSignTransition(false);
    }

    Real getValue(const State& state) const {
        return mobod.getOneQ(state, 0) - lockangle;
    }

    void handleEvent
       (State& s, Real accuracy, const Vector& yWeights, 
        const Vector& ooConstraintTols, 
		Stage& lowestModified, bool& shouldTerminate) const 
    {
        const SimbodyMatterSubsystem& matter = mbs.getMatterSubsystem();
        assert(lock.isDisabled(s));
        assert(dlock.isDisabled(s));

        const Vector uin = s.getU();
        cout << "BEFORE u=" << uin << endl;

        SpatialVec PG = matter.calcSystemMomentumAboutGroundOrigin(s);

        printf("Locking: BEFORE q=%.15g\n",
            mobod.getOneQ(s,0));
        printf("  %5g mom=%g,%g E=%g\n", s.getTime(),
            PG[0].norm(), PG[1].norm(), mbs.calcEnergy(s));


        Vector& mobilityForces = 
            mbs.updMobilityForces(s,Stage::Dynamics);
        Vector_<SpatialVec>& bodyForces = 
            mbs.updRigidBodyForces(s,Stage::Dynamics);

        // Kill off coriolis effects.
        s.updU() = 0;

        // Enable impact constraint
        dlock.enable(s);

        const Real coefRest = 0;
        dlock.setAcceleration(s, -(1+coefRest)*uin[1]);

        cout << "ConstAcc=" << dlock.getAcceleration(s)
             << " (def=" << dlock.getDefaultAcceleration() << ")\n";

	    mbs.realize(s, Stage::Dynamics);
        cout << "non-impulsive mobForces=" <<  mobilityForces << endl;
        cout << "non-impulsive bodyForces=" <<  bodyForces << endl;

        // Cancel applied force "impulses"
        mobilityForces = 0;
        bodyForces = SpatialVec(Vec3(0));

        //// Cancel coriolis "impulse"
        //for (MobilizedBodyIndex bx(1); bx < matter.getNumBodies(); ++bx)
        //    bodyForces[bx] += matter.getTotalCentrifugalForces(s, bx);

        mbs.realize(s, Stage::Acceleration);
        const Vector deltaU = s.getUDot();
        cout << "deltaU=" << deltaU << endl;

        s.updU() = uin + deltaU;
        dlock.disable(s);
        lock.enable(s);

        mbs.realize(s, Stage::Velocity);


        cout << "AFTER u=" << s.getU() << endl;
        printf("Locked: AFTER q=%.15g\n",
            mobod.getOneQ(s,0));

        PG = matter.calcSystemMomentumAboutGroundOrigin(s);
        printf("  %5g mom=%g,%g E=%g\n", s.getTime(),
            PG[0].norm(), PG[1].norm(), mbs.calcEnergy(s));
        cout << "  uerr=" << s.getUErr() << endl;
		lowestModified = Stage::Instance;
    }

private:
	const MultibodySystem&                  mbs; 
	const MobilizedBody&                    mobod;
    const Real                              lockangle;
    const Constraint::ConstantSpeed&        lock;
    const Constraint::ConstantAcceleration& dlock;
};

class LockOff: public TriggeredEventHandler {
public:
    LockOff(const MultibodySystem& system,
        Constraint::ConstantSpeed& lock,
        Real low, Real high) 
    :   TriggeredEventHandler(Stage::Acceleration), 
        m_system(system), m_lock(lock), 
        m_low(low), m_high(high)
    { 
	    getTriggerInfo().setTriggerOnRisingSignTransition(false);
    }

    Real getValue(const State& state) const {
        if (m_lock.isDisabled(state)) return 0;
        const Real f = m_lock.getMultiplier(state);
        const Real mid = (m_high+m_low)/2;
        return f > mid ? m_high - f : f - m_low;
    }

    void handleEvent
       (State& s, Real accuracy, const Vector& yWeights, 
        const Vector& ooConstraintTols, 
		Stage& lowestModified, bool& shouldTerminate) const 
    {
        assert(!m_lock.isDisabled(s));

        m_system.realize(s, Stage::Acceleration);
        printf("LockOff disabling at t=%g lambda=%g",
            s.getTime(), m_lock.getMultiplier(s));
        cout << " Triggers=" << s.getEventTriggers() << endl;

        m_lock.disable(s);
		lowestModified = Stage::Instance;
    }

private:
	const MultibodySystem&                  m_system; 
    const Constraint::ConstantSpeed&        m_lock;
    const Real                              m_low;
    const Real                              m_high;
};

static const Real Deg2Rad = (Real)SimTK_DEGREE_TO_RADIAN,
                  Rad2Deg = (Real)SimTK_RADIAN_TO_DEGREE;



static Real g = 9.8;

int main(int argc, char** argv) {
    static const Transform GroundFrame;
    static const Rotation ZUp(UnitVec3(XAxis), XAxis, UnitVec3(YAxis), ZAxis);
    static const Vec3 TestLoc(1,0,0);

  try { // If anything goes wrong, an exception will be thrown.

        // CREATE MULTIBODY SYSTEM AND ITS SUBSYSTEMS
    MultibodySystem             mbs;

    SimbodyMatterSubsystem      matter(mbs);
    GeneralForceSubsystem       forces(mbs);
    Force::Gravity              gravity(forces, matter, Vec3(0, -g, 0));

        // ADD BODIES AND THEIR MOBILIZERS
    const Vec3 thighHDim(.5,2,.25); 
    const Real thighVol=8*thighHDim[0]*thighHDim[1]*thighHDim[2];
    const Vec3 calfHDim(.25,2,.125); 
    const Real calfVol=8*calfHDim[0]*calfHDim[1]*calfHDim[2];
    const Real density = 1000; // water
    const Real thighMass = density*thighVol, calfMass = density*calfVol;
    Body::Rigid thighBody = 
        Body::Rigid(MassProperties(10*thighMass, Vec3(0), 
                        10*thighMass*Gyration::brick(thighHDim)))
                    .addDecoration(Transform(), DecorativeBrick(thighHDim)
                                                .setColor(Red).setOpacity(.3));
    Body::Rigid calfBody = 
        Body::Rigid(MassProperties(calfMass, Vec3(0), 
                        calfMass*Gyration::brick(calfHDim)))
                    .addDecoration(Transform(), DecorativeBrick(calfHDim)
                                                .setColor(Blue).setOpacity(.3));
    Body::Rigid footBody = 
        Body::Rigid(MassProperties(10*calfMass, Vec3(0), 
                        10*calfMass*Gyration::brick(calfHDim)))
                    .addDecoration(Transform(), DecorativeBrick(calfHDim)
                                                .setColor(Black).setOpacity(.3));
    MobilizedBody::Pin thigh(matter.Ground(), Vec3(0),
                             thighBody, Vec3(0,thighHDim[1],0));
    MobilizedBody::Pin calf(thigh, Vec3(0,-thighHDim[1],0),
                             calfBody, Vec3(0,calfHDim[1],0));
    MobilizedBody::Pin foot(calf, Vec3(0,-calfHDim[1],0),
                             footBody, Vec3(0,calfHDim[1],0));
    //Constraint::PrescribedMotion(matter, 
    //    new Function::Constant(Pi/4,1), foot, MobilizerQIndex(0));

    Constraint::ConstantSpeed lock(calf,0);
    lock.setDisabledByDefault(true);

    Constraint::ConstantAcceleration dlock(calf,0);
    dlock.setDisabledByDefault(true);


    VTKEventReporter& reporter = *new VTKEventReporter(mbs, ReportInterval);
    VTKVisualizer& viz = reporter.updVisualizer();
    mbs.updDefaultSubsystem().addEventReporter(&reporter);

    //ExplicitEulerIntegrator integ(mbs);
    //CPodesIntegrator integ(mbs,CPodes::BDF,CPodes::Newton);
    //RungeKuttaFeldbergIntegrator integ(mbs);
    //RungeKuttaMersonIntegrator integ(mbs);
    RungeKutta3Integrator integ(mbs);
    //VerletIntegrator integ(mbs);

    StateSaver& stateSaver = *new StateSaver(mbs,lock,integ,ReportInterval);
    mbs.updDefaultSubsystem().addEventReporter(&stateSaver);

    mbs.updDefaultSubsystem().addEventHandler
       (new LockOn(mbs,calf,0,lock,dlock));

    mbs.updDefaultSubsystem().addEventHandler
       (new LockOff(mbs,lock,-20000,20000));
  
    State s = mbs.realizeTopology(); // returns a reference to the the default state
    mbs.realizeModel(s); // define appropriate states for this System
	mbs.realize(s, Stage::Instance); // instantiate constraints if any

    thigh.setAngle(s, 90*Deg2Rad);
    calf.setAngle(s, 90*Deg2Rad);
    //calf.setRate(s, -10);

    mbs.realize(s, Stage::Velocity);
    viz.report(s);

    mbs.realize(s, Stage::Acceleration);


    cout << "q=" << s.getQ() << endl;
    cout << "u=" << s.getU() << endl;
    cout << "qerr=" << s.getQErr() << endl;
    cout << "uerr=" << s.getUErr() << endl;
    cout << "udoterr=" << s.getUDotErr() << endl;
    cout << "mults=" << s.getMultipliers() << endl;
    cout << "qdot=" << s.getQDot() << endl;
    cout << "udot=" << s.getUDot() << endl;
    cout << "qdotdot=" << s.getQDotDot() << endl;
    viz.report(s);

    cout << "Initialized configuration shown. Ready? ";
    char c;
    cin >> c;

    
    // Simulate it.
    const clock_t start = clock();



    // TODO: misses some transitions if interpolating
    //integ.setAllowInterpolation(false);
    integ.setAccuracy(1e-1);
    TimeStepper ts(mbs, integ);
    ts.initialize(s);
    ts.stepTo(RunTime);

    const double timeInSec = (double)(clock()-start)/CLOCKS_PER_SEC;
    const int evals = integ.getNumRealizations();
    cout << "Done -- took " << integ.getNumStepsTaken() << " steps in " <<
        timeInSec << "s for " << ts.getTime() << "s sim (avg step=" 
        << (1000*ts.getTime())/integ.getNumStepsTaken() << "ms) " 
        << (1000*ts.getTime())/evals << "ms/eval\n";

    printf("Using Integrator %s at accuracy %g:\n", 
        integ.getMethodName(), integ.getAccuracyInUse());
    printf("# STEPS/ATTEMPTS = %d/%d\n", integ.getNumStepsTaken(), integ.getNumStepsAttempted());
    printf("# ERR TEST FAILS = %d\n", integ.getNumErrorTestFailures());
    printf("# REALIZE/PROJECT = %d/%d\n", integ.getNumRealizations(), integ.getNumProjections());

    while(true) {
        for (int i=0; i < stateSaver.getNumSavedStates(); ++i) {
            viz.report(stateSaver.getState(i));
            //vtk.report(saveEm[i]); // half speed
        }
        getchar();
    }

  } 
  catch (const std::exception& e) {
    printf("EXCEPTION THROWN: %s\n", e.what());
    exit(1);
  }
  catch (...) {
    printf("UNKNOWN EXCEPTION THROWN\n");
    exit(1);
  }

}