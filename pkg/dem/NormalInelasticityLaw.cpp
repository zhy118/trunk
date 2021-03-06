/*************************************************************************
*  Copyright (C) 2008 by Jérôme DURIEZ                                   *
*  duriez@geo.hmg.inpg.fr                                                *
*                                                                        *
*  This program is free software; it is licensed under the terms of the  *
*  GNU General Public License v2 or later. See file LICENSE for details. *
*************************************************************************/

#include<yade/pkg/dem/NormalInelasticityLaw.hpp>

#include<yade/pkg/dem/NormalInelasticMat.hpp>
#include<yade/core/Omega.hpp>
#include<yade/core/Scene.hpp>

YADE_PLUGIN((Law2_ScGeom6D_NormalInelasticityPhys_NormalInelasticity));



void Law2_ScGeom6D_NormalInelasticityPhys_NormalInelasticity::go(shared_ptr<IGeom>& iG, shared_ptr<IPhys>& iP, Interaction* contact)
{
	int id1 = contact->getId1();
	int id2 = contact->getId2();
// 	cout << "contact entre " << id1 << " et " << id2;

	NormalInelasticMat* Mat1 = static_cast<NormalInelasticMat*>(Body::byId(id1,scene)->material.get());
	ScGeom6D* geom		= YADE_CAST<ScGeom6D*>(iG.get());
	NormalInelasticityPhys* currentContactPhysics = YADE_CAST<NormalInelasticityPhys*> (iP.get());

	Vector3r& shearForce 			= currentContactPhysics->shearForce;

	if (contact->isFresh(scene))
		{
		shearForce			= Vector3r::Zero();
		currentContactPhysics->previousun=0.0;
		currentContactPhysics->previousFn=0.0;
		currentContactPhysics->unMax=0.0;
		}

	un = geom->penetrationDepth; // >0 for real penetration

// Check if there is a real overlap or not. The Ig2... seems to let exist interactions with negative un (= no overlap). Such interactions seem then to have to be deleted here.
        if (   un < 0      )
        {
		 scene->interactions->requestErase(contact);// this, among other things, resets the interaction : geometry and physics variables (as forces, ...) are reset to defaut values
		 return;
        }



//	********	Computation of normal Force : depends of the history			*******	 //

// 	cout << " Dans Law2 valeur de kn : " << currentContactPhysics->kn << endl;
// 	cout << "un = " << un << " alors que unMax = "<< currentContactPhysics->unMax << " et previousun = " << currentContactPhysics->previousun << " et previousFn =" << currentContactPhysics->previousFn << endl;

	if(un >= currentContactPhysics->unMax)	// case of virgin load : on the "principal line" (limit state of the (un,Fn) space)
	{
		Fn = currentContactPhysics->knLower*un;
		currentContactPhysics->unMax = std::abs(un);
// 		cout << "je suis dans le calcul normal " << endl;
	}
	else// a priori then we need a greater stifness. False in the case when we are already on the limit state, but a correction below will then do the job
	{
		currentContactPhysics->kn = currentContactPhysics->knLower* Mat1->coeff_dech;
		// Incremental computation of the new normal force :
		Fn = currentContactPhysics->previousFn + currentContactPhysics->kn * (un-currentContactPhysics->previousun);
// 		cout << "je suis dans l'autre calcul" << endl;
		if(std::abs(Fn) > std::abs(currentContactPhysics->knLower * un))	// check if the limit state is not violated
		{
		    Fn = currentContactPhysics->knLower*un;
// 		    cout <<  "j'etais dans l'autre calcul mais j'ai corrige pour ne pas depasser la limite" << endl;
		}
		if(Fn < 0.0 )	// check to stay >=0
		{
			Fn = 0;
// 			cout << "j'ai corrige pour ne pas etre negatif" << endl;
		}
	}
	currentContactPhysics->normalForce	= Fn*geom->normal;
// 	cout << "Fn appliquee " << Fn << endl << endl;

	// actualisation :
	currentContactPhysics->previousFn = Fn;
	currentContactPhysics->previousun = un;

// 	*** End of computation of normal force *** //



//	********	Tangential force				*******	 //
        if (   un < 0      )
        { // BREAK due to tension
		 scene->interactions->requestErase(contact); return;
		 // probably not useful anymore
                currentContactPhysics->normalForce = Vector3r::Zero();
                currentContactPhysics->shearForce = Vector3r::Zero();
        }
        else
        {
		// Correction of previous shear force to take into account the change in normal:
		shearForce = 	geom->rotate(currentContactPhysics->shearForce);
		// Update of shear force corresponding to shear displacement increment:
		shearForce -= currentContactPhysics->ks * geom->shearIncrement();

                Fs = currentContactPhysics->shearForce.norm();
                maxFs = std::max((Real) 0,Fn*currentContactPhysics->tangensOfFrictionAngle);

                if ( Fs  > maxFs )
                {
			maxFs = max((Real) 0, Fn * currentContactPhysics->tangensOfFrictionAngle);

			maxFs = maxFs / Fs;
			if (maxFs>1)
                        	cerr << "maxFs>1!!!!!!!!!!!!!!!!!!!" << endl;
                    	shearForce *= maxFs;
			if (Fn<0)  currentContactPhysics->normalForce = Vector3r::Zero();
                }


                f	= currentContactPhysics->normalForce + shearForce;
		scene->forces.addForce (id1,-f);
		scene->forces.addForce (id2, f);
		scene->forces.addTorque(id1,-(geom->radius1-0.5*geom->penetrationDepth)* geom->normal.cross(f));
		scene->forces.addTorque(id2, -(geom->radius2-0.5*geom->penetrationDepth)* geom->normal.cross(f));
// 	*** End of computation of tangential force *** //


//	********	Moment law				*******	 //

		if(momentRotationLaw)
		{
			currentContactPhysics->moment_twist = (geom->getTwist()*currentContactPhysics->kr)*geom->normal ;
			currentContactPhysics->moment_bending = geom->getBending() * currentContactPhysics->kr;

			moment = currentContactPhysics->moment_twist + currentContactPhysics->moment_bending;

// 	Limitation by plastic threshold of this part of the moment caused by relative twist and bending
			if (!momentAlwaysElastic)
			{
				Real normeMomentMax = currentContactPhysics->forMaxMoment * std::fabs(Fn);
				if(moment.norm()>normeMomentMax)
					{
					moment *= normeMomentMax/moment.norm();
					}
			}
			scene->forces.addTorque(id1,-moment);
			scene->forces.addTorque(id2, moment);
		}
//	********	Moment law END				*******	 //
    }
}



