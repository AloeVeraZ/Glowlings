#pragma once
#include <math.h>

// Relative 3D orientation for horizontal heading. Integrating just gyro dot
// gravity accumulates a false heading around closed roll/pitch paths. Keep
// all rotations together, then extract their twist about the starting gravity.
// This does not supply a compass: gyro bias can still cause heading drift.
class MotionHeading {
 public:
  void reset() { seeded=false; angle=0; }
  float value() const { return angle; }

  void update(const float up[3],const float gyro[3],float dt,bool active) {
    if (!seeded) {
      for (int i=0;i<3;++i) referenceUp[i]=up[i];
      q[0]=1; q[1]=q[2]=q[3]=0;
      seeded=true;
    }
    if (!active) return;
    float speed=sqrtf(gyro[0]*gyro[0]+gyro[1]*gyro[1]+gyro[2]*gyro[2]);
    // Input is bias-corrected. Gate only quiet residual noise, using the whole
    // vector so diagonal turns retain their axis and slow returns are counted.
    if (speed>.03f) {
      float half=speed*dt*.5f,k=sinf(half)/speed;
      multiply(cosf(half),gyro[0]*k,gyro[1]*k,gyro[2]*k);
    }
    float gyroHeading=angle;
    readHeading(gyroHeading);

    // Align predicted body-frame gravity to the existing fused gravity using
    // a small rotation perpendicular to gravity. Preserve the original
    // vertical filter; this correction belongs only to the heading tracker.
    float v[3]={q[1],q[2],q[3]};
    float cross[3]={v[1]*referenceUp[2]-v[2]*referenceUp[1],
                    v[2]*referenceUp[0]-v[0]*referenceUp[2],
                    v[0]*referenceUp[1]-v[1]*referenceUp[0]};
    float predicted[3];
    predicted[0]=referenceUp[0]-2*q[0]*cross[0]+2*(v[1]*cross[2]-v[2]*cross[1]);
    predicted[1]=referenceUp[1]-2*q[0]*cross[1]+2*(v[2]*cross[0]-v[0]*cross[2]);
    predicted[2]=referenceUp[2]-2*q[0]*cross[2]+2*(v[0]*cross[1]-v[1]*cross[0]);
    float axis[3]={up[1]*predicted[2]-up[2]*predicted[1],
                   up[2]*predicted[0]-up[0]*predicted[2],
                   up[0]*predicted[1]-up[1]*predicted[0]};
    float sine=sqrtf(axis[0]*axis[0]+axis[1]*axis[1]+axis[2]*axis[2]);
    float dot=up[0]*predicted[0]+up[1]*predicted[1]+up[2]*predicted[2];
    if (sine>.000001f) {
      float half=atan2f(sine,dot)*(1-expf(-dt/.15f))*.5f,k=sinf(half)/sine;
      multiply(cosf(half),axis[0]*k,axis[1]*k,axis[2]*k);
      // Acceleration measures tilt, not heading. Keep its correction from
      // manufacturing a yaw turn when gravity moves around a circle.
      float correctedHeading=gyroHeading;
      if (readHeading(correctedHeading)) {
        float half=(correctedHeading-gyroHeading)*.5f,k=sinf(half);
        multiplyWorld(cosf(half),referenceUp[0]*k,referenceUp[1]*k,referenceUp[2]*k);
      }
    }
    float length=sqrtf(q[0]*q[0]+q[1]*q[1]+q[2]*q[2]+q[3]*q[3]);
    if (length<.01f || !isfinite(length)) { reset(); return; }
    for (float &component:q) component/=length;
    readHeading(angle);
  }

 private:
  bool readHeading(float &value) const {
    float twist=q[1]*referenceUp[0]+q[2]*referenceUp[1]+q[3]*referenceUp[2];
    // Twist is undefined for an exact 180-degree swing. Keep the preceding
    // heading there instead of inventing a change at the singular pose.
    if (q[0]*q[0]+twist*twist>.000001f) {
      value=-2*atan2f(twist,q[0]);
      if (value>3.14159265f) value-=6.28318531f;
      if (value<-3.14159265f) value+=6.28318531f;
      return true;
    }
    return false;
  }
  void multiplyWorld(float w,float x,float y,float z) {
    float next[4]={w*q[0]-x*q[1]-y*q[2]-z*q[3],
                   w*q[1]+x*q[0]+y*q[3]-z*q[2],
                   w*q[2]-x*q[3]+y*q[0]+z*q[1],
                   w*q[3]+x*q[2]-y*q[1]+z*q[0]};
    for (int i=0;i<4;++i) q[i]=next[i];
  }
  void multiply(float w,float x,float y,float z) {
    float next[4]={q[0]*w-q[1]*x-q[2]*y-q[3]*z,
                   q[0]*x+q[1]*w+q[2]*z-q[3]*y,
                   q[0]*y-q[1]*z+q[2]*w+q[3]*x,
                   q[0]*z+q[1]*y-q[2]*x+q[3]*w};
    for (int i=0;i<4;++i) q[i]=next[i];
  }
  float q[4]={1,0,0,0},referenceUp[3]={0,0,1},angle=0;
  bool seeded=false;
};
