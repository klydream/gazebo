/*
 * Copyright 2011 Nate Koenig & Andrew Howard
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
*/
/* Desc: Link class
 * Author: Nate Koenig
 * Date: 13 Feb 2006
 */

#include <sstream>
#include <float.h>

#include "msgs/msgs.h"

#include "common/Events.hh"
#include "math/Quaternion.hh"
#include "common/Console.hh"
#include "common/Exception.hh"

#include "sensors/SensorManager.hh"

#include "physics/Model.hh"
#include "physics/World.hh"
#include "physics/PhysicsEngine.hh"
#include "physics/Collision.hh"
#include "physics/Link.hh"

#include "transport/Publisher.hh"

using namespace gazebo;
using namespace physics;

////////////////////////////////////////////////////////////////////////////////
// Constructor
Link::Link(EntityPtr parent)
    : Entity(parent)
{
  this->AddType(Base::LINK);
  this->inertial.reset(new Inertial);
  this->parentJoints.clear();
  this->childJoints.clear();
}


////////////////////////////////////////////////////////////////////////////////
// Destructor
Link::~Link()
{
  std::vector<Entity*>::iterator iter;

  for (unsigned int i=0; i < this->visuals.size(); i++)
  {
    msgs::Visual msg;
    msg.set_name(this->visuals[i] );
    msg.set_delete_me( true );
    this->visPub->Publish(msg);
  }
  this->visuals.clear();

  if (this->cgVisuals.size() > 0)
  {
    for (unsigned int i=0; i < this->cgVisuals.size(); i++)
    {
      msgs::Visual msg;
      msg.set_name(this->cgVisuals[i] );
      msg.set_delete_me( true );
      this->visPub->Publish(msg);
    }
    this->cgVisuals.clear();
  }

  this->visPub.reset();
  this->sensors.clear();
}

////////////////////////////////////////////////////////////////////////////////
// Load the body
void Link::Load(sdf::ElementPtr &_sdf)
{
  Entity::Load(_sdf);

  if (!this->IsStatic())
  {
    if (this->sdf->HasElement("inertial"))
      this->inertial->Load(this->sdf->GetElement("inertial"));
    else
      gzerr << "Non-static body has no interial sdf element.\n";
  }

  // before loading child collsion, we have to figure out of selfCollide is true
  // and modify parent class Entity so this body has its own spaceId
  this->SetSelfCollide(this->sdf->GetValueBool("self_collide"));
  this->sdf->GetAttribute("self_collide")->SetUpdateFunc(
      boost::bind(&Link::GetSelfCollide,this));

  // TODO: this shouldn't be in the physics sim
  if (this->sdf->HasElement("visual"))
  {
    sdf::ElementPtr visualElem = this->sdf->GetElement("visual");
    while (visualElem)
    {
      msgs::Visual msg = msgs::VisualFromSDF(visualElem);
      msg.set_name(this->GetCompleteScopedName() + "::" + msg.name());
      msg.set_parent_name( this->GetCompleteScopedName() );
      msg.set_is_static( this->IsStatic() );

      this->visPub->Publish(msg);
      this->visuals.push_back(msg.name());

      visualElem = visualElem->GetNextElement(); 
    }
  }
 
  // Load the geometries
  if (this->sdf->HasElement("collision"))
  {
    sdf::ElementPtr collisionElem = this->sdf->GetElement("collision");
    while (collisionElem)
    {
      // Create and Load a collision, which will belong to this body.
      this->LoadCollision(collisionElem);
      collisionElem = collisionElem->GetNextElement(); 
    }
  }

  if (this->sdf->HasElement("sensor"))
  {
    sdf::ElementPtr sensorElem = this->sdf->GetElement("sensor");
    while (sensorElem)
    {
      std::string sensorName = sensors::SensorManager::Instance()->LoadSensor(
          sensorElem, this->GetCompleteScopedName());
      this->sensors.push_back(sensorName);
      sensorElem = sensorElem->GetNextElement(); 
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
// Initialize the body
void Link::Init()
{
  Base_V::iterator iter;
  for (iter = this->children.begin(); iter != this->children.end(); iter++)
  {
    if ((*iter)->HasType(Base::COLLISION))
    {
      CollisionPtr g = boost::shared_static_cast<Collision>(*iter);
      g->Init();
    }
  }

  this->SetKinematic( this->sdf->GetValueBool("kinematic") );

  // If no collisions are attached, then don't let gravity affect the body.
  if (this->children.size()==0 || !this->sdf->GetValueBool("gravity"))
    this->SetGravityMode(false);

  // global-inertial damping is implemented in ode svn trunk
  if (this->inertial)
  {
    this->SetLinearDamping( this->inertial->GetLinearDamping() );
    this->SetAngularDamping( this->inertial->GetAngularDamping() );
  }

  this->linearAccel.Set(0,0,0);
  this->angularAccel.Set(0,0,0);

  /// Attach mesh for CG visualization
  /// Add a renderable visual for CG, make visible in Update()
  /// TODO: this shouldn't be in the physics sim
  /*if (this->mass.GetAsDouble() > 0.0)
  {
    std::ostringstream visname;
    visname << this->GetCompleteScopedName() + ":" + this->GetName() << "_CGVISUAL" ;

    msgs::Visual msg;
    msg.set_name( visname.str() );
    msg.set_parent_id( this->comEntity->GetCompleteScopedName() );
    msg.set_render_type( msgs::Visual::MESH_RESOURCE );
    msg.set_mesh( "unit_box" );
    msg.set_material( "Gazebo/RedGlow" );
    msg.set_cast_shadows( false );
    msg.set_attach_axes( true );
    msg.set_visible( false );
    msgs::Set(msg.mutable_scale(), math::Vector3(0.1, 0.1, 0.1));
    this->vis_pub->Publish(msg);
    this->cgVisuals.push_back( msg.header().str_id() );

    if (this->children.size() > 1)
    {
      msgs::Visual g_msg;
      g_msg.set_name( visname.str() + "_connectors" );

      g_msg.set_parent_id( this->comEntity->GetCompleteScopedName() );
      g_msg.set_render_type( msgs::Visual::LINE_LIST );
      g_msg.set_attach_axes( false );
      g_msg.set_material( "Gazebo/GreenGlow" );
      g_msg.set_visible( false );

      // Create a line to each collision
      for (Base_V::iterator giter = this->children.begin(); 
           giter != this->children.end(); giter++)
      {
        EntityPtr e = boost::shared_dynamic_cast<Entity>(*giter);

        msgs::Point *pt;
        pt = g_msg.add_points();
        pt->set_x(0);
        pt->set_y(0);
        pt->set_z(0);

        pt = g_msg.add_points();
        pt->set_x(e->GetRelativePose().pos.x);
        pt->set_y(e->GetRelativePose().pos.y);
        pt->set_z(e->GetRelativePose().pos.z);
      }
      this->vis_pub->Publish(msg);
      this->cgVisuals.push_back( g_msg.header().str_id() );
    }
  }*/

  this->enabled = true;

  // DO THIS LAST!
  sdf::ElementPtr originElem = this->sdf->GetOrCreateElement("origin");
  this->SetRelativePose(originElem->GetValuePose("pose"));
  this->SetInitialRelativePose(originElem->GetValuePose("pose"));
}

////////////////////////////////////////////////////////////////////////////////
// Finalize the body
void Link::Fini()
{
  std::vector<std::string>::iterator iter;
  for(iter = this->visuals.begin(); iter != this->visuals.end(); iter++)
  {
    msgs::Request *msg = msgs::CreateRequest("entity_delete", *iter);
    this->requestPub->Publish(*msg, true);
  }

  for(iter = this->cgVisuals.begin(); iter != this->cgVisuals.end(); iter++)
  {
    msgs::Request *msg = msgs::CreateRequest("entity_delete", *iter);
    this->requestPub->Publish(*msg, true);
  }

  Entity::Fini();
}

////////////////////////////////////////////////////////////////////////////////
/// Update the parameters using new sdf values
void Link::UpdateParameters( sdf::ElementPtr &_sdf )
{
  Entity::UpdateParameters(_sdf);

  if (this->sdf->HasElement("inertial"))
  {
    sdf::ElementPtr inertialElem = this->sdf->GetElement("inertial");
    this->inertial->UpdateParameters( inertialElem );
  }

  this->sdf->GetAttribute("gravity")->SetUpdateFunc(
      boost::bind(&Link::GetGravityMode,this));
  this->sdf->GetAttribute("kinematic")->SetUpdateFunc(
      boost::bind(&Link::GetKinematic,this));


  if (this->sdf->GetValueBool("gravity") != this->GetGravityMode())
    this->SetGravityMode( this->sdf->GetValueBool("gravity") );

  // before loading child collsiion, we have to figure out if 
  // selfCollide is true and modify parent class Entity so this 
  // body has its own spaceId
  this->SetSelfCollide(this->sdf->GetValueBool("self_collide"));

  // TODO: this shouldn't be in the physics sim
  if (this->sdf->HasElement("visual"))
  {
    sdf::ElementPtr visualElem = this->sdf->GetElement("visual");
    while (visualElem)
    {
      // TODO: Update visuals properly
      msgs::Visual msg = msgs::VisualFromSDF(visualElem);

      msg.set_name(this->GetCompleteScopedName() + "::" + msg.name());
      msg.set_parent_name(this->GetCompleteScopedName());
      msg.set_is_static(this->IsStatic());

      this->visPub->Publish(msg);

      visualElem = visualElem->GetNextElement(); 
    }
  }

  if (this->sdf->HasElement("collision"))
  {
    sdf::ElementPtr collisionElem = this->sdf->GetElement("collision");
    while (collisionElem)
    {
      CollisionPtr collision = boost::shared_dynamic_cast<Collision>(
          this->GetChild( collisionElem->GetValueString("name") ) );

      if (collision)
        collision->UpdateParameters(collisionElem);
      collisionElem = collisionElem->GetNextElement();
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
/// Set the collide mode of the body
void Link::SetCollideMode( const std::string &m )
{
  Base_V::iterator giter;

  unsigned int collideBits;

  if (m == "all")
    collideBits =  GZ_ALL_COLLIDE;
  else if (m == "none")
    collideBits =  GZ_NONE_COLLIDE;
  else if (m == "sensors")
    collideBits = GZ_SENSOR_COLLIDE;
  else if (m == "ghost")
    collideBits = GZ_GHOST_COLLIDE;
  else
  {
    gzerr << "Unknown collide mode[" << m << "]\n";
    return;
  }

  // TODO: Put this back in
  /*for (giter = this->collisions.begin(); giter != this->collisions.end(); giter++)
  {
    (*giter)->SetCategoryBits(collideBits);
    (*giter)->SetCollideBits(collideBits);
  }*/
}

////////////////////////////////////////////////////////////////////////////////
// Return Self-Collision Setting
bool Link::GetSelfCollide()
{
  return this->sdf->GetValueBool("self_collide");
}

////////////////////////////////////////////////////////////////////////////////
/// Set the laser retro reflectiveness of this body
void Link::SetLaserRetro(float retro)
{
  Base_V::iterator iter;

  for (iter = this->children.begin(); iter != this->children.end(); iter++)
  {
    if ((*iter)->HasType(Base::COLLISION))
      boost::shared_static_cast<Collision>(*iter)->SetLaserRetro( retro );
  }
}

////////////////////////////////////////////////////////////////////////////////
// Update the body
void Link::Update()
{
  // Apply our linear accel
  //this->SetForce(this->linearAccel);

  // Apply our angular accel
  //this->SetTorque(this->angularAccel);

  // FIXME: FIXME: @todo: @todo: race condition on factory-based model loading!!!!!
   /*if (this->GetEnabled() != this->enabled)
   {
     this->enabled = this->GetEnabled();
     this->enabledSignal(this->enabled);
   }*/
}

// Load a new collision helper function
void Link::LoadCollision( sdf::ElementPtr &_sdf )
{
  CollisionPtr collision;
  std::string type = _sdf->GetElement("geometry")->GetFirstElement()->GetName();

  /*if (type == "heightmap" || type == "map")
    this->SetStatic(true);
    */

  collision = this->GetWorld()->GetPhysicsEngine()->CreateCollision( type, 
      boost::shared_static_cast<Link>(shared_from_this()) );

  if (!collision)
    gzthrow("Unknown Collisionetry Type["+type +"]");

  collision->Load(_sdf);
}

CollisionPtr Link::GetCollisionById(unsigned int _id) const
{
  return boost::shared_dynamic_cast<Collision>(this->GetById(_id));
}

CollisionPtr Link::GetCollision(const std::string &_name)
{
  CollisionPtr result;
  Base_V::const_iterator biter;
  for (biter=this->children.begin(); biter != this->children.end(); biter++)
  {
    if ((*biter)->GetName() == _name)
    {
      result = boost::shared_dynamic_cast<Collision>(*biter);
      break;
    }
  }

  return result;
}

////////////////////////////////////////////////////////////////////////////////
/// Set the linear acceleration of the body
void Link::SetLinearAccel(const math::Vector3 &accel)
{
  //this->SetEnabled(true); Disabled this line to make autoDisable work
  this->linearAccel = accel;// * this->GetMass();
}



////////////////////////////////////////////////////////////////////////////////
/// Set the angular acceleration of the body
void Link::SetAngularAccel(const math::Vector3 &accel)
{
  //this->SetEnabled(true); Disabled this line to make autoDisable work
  this->angularAccel = accel * this->inertial->GetMass();
}

////////////////////////////////////////////////////////////////////////////////
/// Get the linear velocity of the body
math::Vector3 Link::GetRelativeLinearVel() const
{
  return this->GetWorldPose().rot.RotateVectorReverse(this->GetWorldLinearVel());
}

////////////////////////////////////////////////////////////////////////////////
/// Get the angular velocity of the body
math::Vector3 Link::GetRelativeAngularVel() const
{
  return this->GetWorldPose().rot.RotateVectorReverse(this->GetWorldAngularVel());
}

////////////////////////////////////////////////////////////////////////////////
/// Get the linear acceleration of the body
math::Vector3 Link::GetRelativeLinearAccel() const
{
  return this->GetRelativeForce() / this->inertial->GetMass();
}

////////////////////////////////////////////////////////////////////////////////
/// Get the angular acceleration of the body
math::Vector3 Link::GetWorldLinearAccel() const
{
  return this->GetWorldForce() / this->inertial->GetMass();
}

////////////////////////////////////////////////////////////////////////////////
/// Get the angular acceleration of the body
math::Vector3 Link::GetRelativeAngularAccel() const
{
  return this->GetRelativeTorque() / this->inertial->GetMass();
}

////////////////////////////////////////////////////////////////////////////////
/// Get the angular acceleration of the body
math::Vector3 Link::GetWorldAngularAccel() const
{
  return this->GetWorldTorque() / this->inertial->GetMass();
}

////////////////////////////////////////////////////////////////////////////////
/// Get the force applied to the body
math::Vector3 Link::GetRelativeForce() const
{
  return this->GetWorldPose().rot.RotateVectorReverse(this->GetWorldForce());
}

////////////////////////////////////////////////////////////////////////////////
/// Get the torque applied to the body
math::Vector3 Link::GetRelativeTorque() const
{
  return this->GetWorldPose().rot.RotateVectorReverse(this->GetWorldTorque());
}

////////////////////////////////////////////////////////////////////////////////
/// Get the model that this body belongs to
ModelPtr Link::GetModel() const
{
  return boost::shared_dynamic_cast<Model>(this->GetParent());
}

////////////////////////////////////////////////////////////////////////////////
/// Get the size of the body
math::Box Link::GetBoundingBox() const
{
  math::Box box;
  Base_V::const_iterator iter;

  box.min.Set(FLT_MAX, FLT_MAX, FLT_MAX);
  box.max.Set(0,0,0);

  for (iter = this->children.begin(); iter != this->children.end(); iter++)
  {
    if ((*iter)->HasType(Base::COLLISION))
      box += boost::shared_static_cast<Collision>(*iter)->GetBoundingBox();
  }

  return box;
}

////////////////////////////////////////////////////////////////////////////////
/// Set whether this entity has been selected by the user through the gui
bool Link::SetSelected( bool s )
{
  Entity::SetSelected(s);

  if (s == false)
    this->SetEnabled(true);

  return true;
}

////////////////////////////////////////////////////////////////////////////////
// Set Mass
void Link::SetInertial(const InertialPtr &/*_inertial*/)
{
  gzwarn << "Link::SetMass is empty\n";
}

////////////////////////////////////////////////////////////////////////////////
// Add a parent joint
void Link::AddParentJoint(JointPtr joint)
{
  this->parentJoints.push_back(joint);
}

////////////////////////////////////////////////////////////////////////////////
// Add a child joint
void Link::AddChildJoint(JointPtr joint)
{
  this->childJoints.push_back(joint);
}

////////////////////////////////////////////////////////////////////////////////
/// Fill a link message
void Link::FillLinkMsg( msgs::Link &_msg )
{
  _msg.set_id(this->GetId());
  _msg.set_name(this->GetCompleteScopedName());
  _msg.set_self_collide(this->GetSelfCollide());
  _msg.set_gravity(this->GetGravityMode());
  _msg.set_kinematic(this->GetKinematic());
  msgs::Set(_msg.mutable_pose(), this->GetRelativePose());

  _msg.add_visual()->CopyFrom(*this->visualMsg);

  _msg.mutable_inertial()->set_mass(this->inertial->GetMass());
  _msg.mutable_inertial()->set_linear_damping(
      this->inertial->GetLinearDamping());
  _msg.mutable_inertial()->set_angular_damping(
      this->inertial->GetAngularDamping() );
  _msg.mutable_inertial()->set_ixx(this->inertial->GetIXX());
  _msg.mutable_inertial()->set_ixy(this->inertial->GetIXY());
  _msg.mutable_inertial()->set_ixz(this->inertial->GetIXZ());
  _msg.mutable_inertial()->set_iyy(this->inertial->GetIYY());
  _msg.mutable_inertial()->set_iyz(this->inertial->GetIYZ());
  _msg.mutable_inertial()->set_izz(this->inertial->GetIZZ());
  msgs::Set(_msg.mutable_inertial()->mutable_pose(), this->inertial->GetPose());

  for (unsigned int j=0; j < this->GetChildCount(); j++)
  {
    if (this->GetChild(j)->HasType(Base::COLLISION))
    {
      CollisionPtr coll = boost::shared_dynamic_cast<Collision>(
          this->GetChild(j));
      coll->FillCollisionMsg(*_msg.add_collision());
    }
  }

  if (this->sdf->HasElement("visual"))
  {
    sdf::ElementPtr visualElem = this->sdf->GetElement("visual");
    while (visualElem)
    {
      msgs::Visual *vis = _msg.add_visual();
      vis->CopyFrom(msgs::VisualFromSDF(visualElem));
      vis->set_name(this->GetCompleteScopedName() + "::" + vis->name());
      vis->set_parent_name(this->GetCompleteScopedName());

      std::cout << "Link Visual[" << vis->DebugString() << "]\n";

      visualElem = visualElem->GetNextElement(); 
    }
  }
}

void Link::ProcessMsg(const msgs::Link &_msg)
{
  if (_msg.id() != this->GetId())
  {
    gzerr << "Incorrect ID\n";
    return;
  }

  this->SetName(_msg.name());

  if(_msg.has_pose())
    this->SetRelativePose(msgs::Convert(_msg.pose()));
  if (_msg.has_self_collide())
    this->SetSelfCollide(_msg.self_collide());
  if (_msg.has_gravity())
    this->SetGravityMode(_msg.gravity());
  if (_msg.has_kinematic())
    this->SetKinematic(_msg.kinematic());
  if (_msg.has_inertial())
    this->inertial->ProcessMsg(_msg.inertial());

  for (int i=0; i < _msg.collision_size(); i++)
  {
    CollisionPtr coll = this->GetCollisionById(_msg.collision(i).id());
    if (coll)
      coll->ProcessMsg(_msg.collision(i));
  }
}


//////////////////////////////////////////////////
unsigned int Link::GetSensorCount() const
{
  return this->sensors.size();
}

//////////////////////////////////////////////////
std::string Link::GetSensorName(unsigned int _i) const
{
  if (_i < this->sensors.size())
    return this->sensors[_i];

  return std::string();
}
