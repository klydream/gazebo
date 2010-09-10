/*
 *  Gazebo - Outdoor Multi-Robot Simulator
 *  Copyright (C) 2003
 *     Nate Koenig & Andrew Howard
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
/*
 * Desc: Controller base class.
 * Author: Nathan Koenig
 * Date: 01 Feb 2007
 * SVN info: $Id$
 */

#include "Timer.hh"
#include "Model.hh"
#include "Sensor.hh"
#include "GazeboError.hh"
#include "GazeboMessage.hh"
#include "XMLConfig.hh"
#include "World.hh"
#include "Controller.hh"
#include "Simulator.hh"
#include "PhysicsEngine.hh"
#include "Global.hh"

using namespace gazebo;

////////////////////////////////////////////////////////////////////////////////
/// Constructor
Controller::Controller(Entity *entity )
{
  Param::Begin(&this->parameters);
  this->nameP = new ParamT<std::string>("name","",1);
  this->alwaysOnP = new ParamT<bool>("alwaysOn", false, 0);
  this->updateRateP = new ParamT<double>("updateRate", 10, 0);
  this->updateRateP->Callback(&Controller::SetUpdateRate, this);
  Param::End();

  if (!dynamic_cast<Model*>(entity) && !dynamic_cast<Sensor*>(entity))
  {
    gzthrow("The parent of a controller must be a Model or a Sensor");
  }

  this->parent = entity;
}

////////////////////////////////////////////////////////////////////////////////
/// Destructor
Controller::~Controller()
{
  delete this->nameP;
  delete this->alwaysOnP;
  delete this->updateRateP;
}

////////////////////////////////////////////////////////////////////////////////
/// Load the controller. Called once on startup
void Controller::Load(XMLConfigNode *node)
{
  XMLConfigNode *childNode;

  if (!this->parent)
    gzthrow("Parent entity has not been set");

  this->typeName = node->GetName();

  this->nameP->Load(node);

  this->alwaysOnP->Load(node);

  this->updateRateP->Load(node);
  this->SetUpdateRate(this->updateRateP->GetValue());
  //printf("updatePeriod loaded %f Rate: %f\n",this->updatePeriod.Double(),**this->updateRateP);

  childNode = node->GetChildByNSPrefix("interface");
  
  // Create the interfaces
  while (childNode)
  {
    libgazebo::Iface *iface=0;

    // Get the type of the interface (eg: laser)
    std::string ifaceType = childNode->GetName();

    // Get the name of the iface
    std::string ifaceName = childNode->GetString("name","",1);

    // Constructor the heirarchical name for the iface
    Entity *p = parent;
    while (p != NULL)
    {
      Model *m = dynamic_cast<Model*>(p);
      if (m)
        ifaceName.insert(0, m->GetName()+"::");
      p = p->GetParent();
    }

    try
    {
      // Use the factory to get a new iface based on the type
      iface = libgazebo::IfaceFactory::NewIface(ifaceType);
    }
    catch (...) //TODO: Show the exception text here (subclass exception?)
    {
      gzmsg(1) << "No libgazebo Iface for the interface[" << ifaceType << "] found. Disabled.\n";
      childNode = childNode->GetNextByNSPrefix("interface");
      continue;
    }
    
    // Create the iface
    try
    {
      iface->Create(World::Instance()->GetGzServer(), ifaceName);
    }
    catch (std::string e)
    {
      gzthrow(e);
    }
    
    this->ifaces.push_back(iface);

    childNode = childNode->GetNextByNSPrefix("interface");
  }

  this->LoadChild(node);
}

////////////////////////////////////////////////////////////////////////////////
/// Save the sensor info in XML format
void Controller::SetUpdateRate(const double &rate)
{

  if (rate == 0)
    this->updatePeriod = 0.0;
  else
    this->updatePeriod = 1.0 / rate;
  this->updateRateP->SetValue(rate); // need this when called externally

}


////////////////////////////////////////////////////////////////////////////////
/// Initialize the controller. Called once on startup.
void Controller::Init()
{
  this->lastUpdate = Simulator::Instance()->GetSimTime();

  this->InitChild();
}

////////////////////////////////////////////////////////////////////////////////
/// Save the controller.
void Controller::Save(std::string &prefix, std::ostream &stream)
{
  std::vector<libgazebo::Iface*>::iterator iter;

  stream << prefix << "<controller:" << this->typeName << " name=\"" << this->nameP->GetValue() << "\">\n";

  stream << prefix << "  " << *(this->updateRateP) << "\n";

  // Ouptut the interfaces
  for (iter = this->ifaces.begin(); iter != this->ifaces.end(); iter++)
  {
    stream << prefix << "  <interface:" << (*iter)->GetType() << " name=\"" << (*iter)->GetId() << "\"/>\n";
  }

  std::string p = prefix + "  ";

  this->SaveChild(p, stream);

  stream << prefix << "</controller:" << this->typeName << ">\n";
}

////////////////////////////////////////////////////////////////////////////////
// Reset the controller
void Controller::Reset()
{
  this->ResetChild();
}

////////////////////////////////////////////////////////////////////////////////
/// Update the controller. Called every cycle.
void Controller::Update()
{
  if (this->IsConnected() || **this->alwaysOnP)
  {
    //DiagnosticTimer timer("Controller[" + this->GetName() +"] Update Timer");

    // round time difference to this->physicsEngine->GetStepTime()
    Time physics_dt = World::Instance()->GetPhysicsEngine()->GetStepTime();

    //timer.Start();

    Time simTime = Simulator::Instance()->GetSimTime();
    if ((simTime-this->lastUpdate-this->updatePeriod)/physics_dt >= 0)
    {
      this->UpdateChild();
      this->lastUpdate = Simulator::Instance()->GetSimTime();
      //timer.Report("Update() dt");
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
/// Finialize the controller. Called once on completion.
void Controller::Fini()
{
  std::vector<libgazebo::Iface*>::iterator iter;

  for (iter=this->ifaces.begin(); iter!=this->ifaces.end(); iter++)
  {
    delete *iter;
  }
  this->ifaces.clear();

  this->FiniChild();
}

////////////////////////////////////////////////////////////////////////////////
/// Return true if an interface is open 
bool Controller::IsConnected() const
{
  std::vector<libgazebo::Iface*>::const_iterator iter;

  // if the alwaysOn flag is true, this controller is connected
  if (this->alwaysOnP->GetValue())
    return true;

  for (iter=this->ifaces.begin(); iter!=this->ifaces.end(); iter++)
  {
    if ((*iter)->GetOpenCount() > 0)
      return true;
  }

  return false;
}

////////////////////////////////////////////////////////////////////////////////
// Get the name of the controller
std::string Controller::GetName() const
{
  return this->nameP->GetValue();
}

////////////////////////////////////////////////////////////////////////////////
// Get a interface of the controller
libgazebo::Iface* Controller::GetIface(std::string type, bool mandatory, int number)
{
  std::vector<libgazebo::Iface*>::iterator iter;
  int order = number;
  libgazebo::Iface *iface = NULL;

  for (iter = this->ifaces.begin(); iter != this->ifaces.end(); iter++)
  {
    if ((*iter)->GetType() == type)
    {
      if (order == 0)
        iface = (*iter);
      else
        order --;
    }
  }
  
  if ((iface == NULL) and mandatory)
  {
    std::ostringstream stream;
    stream << "Controller " << this->GetName() << "trying to get " << type << " interface but it is not defined";
    gzthrow(stream.str());
  }
  return iface;
}

void Controller::GetInterfaceNames(std::vector<std::string>& list) const{
  
  std::vector<libgazebo::Iface*>::const_iterator iter;
  
  for (iter = this->ifaces.begin(); iter != this->ifaces.end(); iter++)
  {
    std::string str;
    str=(*iter)->GetId()+">>"+(*iter)->GetType();
    list.push_back(str);
    
  }


}  
