/*
** Copyright 2007-2025, RTE (https://www.rte-france.com)
** See AUTHORS.txt
** SPDX-License-Identifier: MPL-2.0
** This file is part of Antares-Simulator,
** Adequacy and Performance assessment for interconnected energy networks.
**
** Antares_Simulator is free software: you can redistribute it and/or modify
** it under the terms of the Mozilla Public Licence 2.0 as published by
** the Mozilla Foundation, either version 2 of the License, or
** (at your option) any later version.
**
** Antares_Simulator is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
** Mozilla Public Licence 2.0 for more details.
**
** You should have received a copy of the Mozilla Public Licence 2.0
** along with Antares_Simulator. If not, see <https://opensource.org/license/mpl-2-0/>.
*/

#include "antares/solver/optimisation/post_process_commands.h"

#include <fstream> // Defines std::ofstream, std::ifstream, std::fstream
#include <iomanip>
#include <spx_constantes_externes.h>
#include <string>

#include "antares/io/outputs/SimulationTableCsv.h"
#include "antares/solver/optimisation/adequacy_patch_csr/adq_patch_curtailment_sharing.h"
#include "antares/solver/optimisation/run-thermal-heuristic.h"
#include "antares/solver/optimisation/weekly_optimization.h"
#include "antares/solver/simulation/adequacy_patch_runtime_data.h"
#include "antares/solver/simulation/common-eco-adq.h"
#include "antares/solver/utils/filename.h"

namespace Antares::Solver::Simulation
{
const uint nbHoursInWeek = 168;

// -----------------------------
// Dispatchable Margin
// -----------------------------
DispatchableMarginPostProcessCmd::DispatchableMarginPostProcessCmd(PROBLEME_HEBDO* problemeHebdo,
                                                                   unsigned int numSpace,
                                                                   const AreaList& areas):
    basePostProcessCommand(problemeHebdo),
    numSpace_(numSpace),
    area_list_(areas)
{
}

void DispatchableMarginPostProcessCmd::execute(const optRuntimeData& opt_runtime_data)
{
    unsigned int hourInYear = opt_runtime_data.hourInTheYear;
    unsigned int year = opt_runtime_data.year;
    area_list_.each(
      [this, &hourInYear, &year](Data::Area& area)
      {
          double* dtgmrg = area.scratchpad[numSpace_].dispatchableGenerationMargin;
          for (uint h = 0; h != nbHoursInWeek; ++h)
          {
              dtgmrg[h] = 0.;
          }

          auto& hourlyResults = problemeHebdo_->ResultatsHoraires[area.index];

          for (const auto& cluster: area.thermal.list.each_enabled_and_not_mustrun())
          {
              const auto& availableProduction = cluster->series.getColumn(year);
              for (uint h = 0; h != nbHoursInWeek; ++h)
              {
                  double production = hourlyResults.ProductionThermique[h]
                                        .ProductionThermiqueDuPalier[cluster->index];
                  dtgmrg[h] += availableProduction[h + hourInYear] - production;
              }
          }
      });
}

// -----------------------------
//  Remix Hydro
// -----------------------------
RemixHydroPostProcessCmd::RemixHydroPostProcessCmd(PROBLEME_HEBDO* problemeHebdo,
                                                   AreaList& areas,
                                                   const Data::Parameters& params,
                                                   unsigned int numSpace):
    basePostProcessCommand(problemeHebdo),
    area_list_(areas),
    numSpace_(numSpace),
    params_(params)
{
}

void RemixHydroPostProcessCmd::execute(const optRuntimeData& opt_runtime_data)
{
    unsigned int hourInYear = opt_runtime_data.hourInTheYear;
    RemixHydroForAllAreas(area_list_, *problemeHebdo_, params_, numSpace_, hourInYear);
}

// ----------------------------------
//  Update marginal price after CSR
// ----------------------------------
using namespace Antares::Data::AdequacyPatch;

UpdateMrgPriceAfterCSRcmd::UpdateMrgPriceAfterCSRcmd(PROBLEME_HEBDO* problemeHebdo,
                                                     AreaList& areas,
                                                     unsigned int numSpace):
    basePostProcessCommand(problemeHebdo),
    area_list_(areas),
    numSpace_(numSpace)
{
}

void UpdateMrgPriceAfterCSRcmd::execute(const optRuntimeData&)
{
    for (uint32_t Area = 0; Area < problemeHebdo_->NombreDePays; Area++)
    {
        auto& hourlyResults = problemeHebdo_->ResultatsHoraires[Area];
        const auto& scratchpad = area_list_[Area]->scratchpad[numSpace_];
        const double unsuppliedEnergyCost = area_list_[Area]->thermal.unsuppliedEnergyCost;
        const bool areaInside = problemeHebdo_->adequacyPatchRuntimeData->areaMode[Area]
                                == physicalAreaInsideAdqPatch;
        for (uint hour = 0; hour < nbHoursInWeek; hour++)
        {
            const bool isHourTriggeredByCsr = problemeHebdo_->adequacyPatchRuntimeData
                                                ->wasCSRTriggeredAtAreaHour(Area, hour);

            // IF UNSP. ENR CSR == 0, MRG. PRICE CSR = MRG. PRICE
            // ELSE, MRG. PRICE CSR = “Unsupplied Energy Cost”
            if (hourlyResults.ValeursHorairesDeDefaillancePositiveCSR[hour] > 0.5 && areaInside)
            {
                hourlyResults.CoutsMarginauxHorairesCSR[hour] = -unsuppliedEnergyCost;
            }
            else
            {
                hourlyResults.CoutsMarginauxHorairesCSR[hour] = hourlyResults
                                                                  .CoutsMarginauxHoraires[hour];
            }

            if (isHourTriggeredByCsr
                && hourlyResults.ValeursHorairesDeDefaillancePositive[hour] > 0.5 && areaInside)
            {
                hourlyResults.CoutsMarginauxHoraires[hour] = -unsuppliedEnergyCost;
            }
        }
    }
}

// -----------------------------
//  DTG margin for adq patch
// -----------------------------
DTGnettingAfterCSRcmd::DTGnettingAfterCSRcmd(PROBLEME_HEBDO* problemeHebdo,
                                             AreaList& areas,
                                             unsigned int numSpace):
    basePostProcessCommand(problemeHebdo),
    area_list_(areas),
    numSpace_(numSpace)
{
}

void DTGnettingAfterCSRcmd::execute(const optRuntimeData&)
{
    // for (uint hour = 0; hour < nbHoursInWeek; hour++)
    // {
    //    if (problemeHebdo_->adequacyPatchRuntimeData->wasCSRTriggeredAtAreaHour(Area, hour);)
    //     {
    //         AdequacyPatchOptimization::solve(,hour)
    //         void AdequacyPatchOptimization::solve(uint weekInTheYear, int hourInTheYear)
    //             ::SIM_RenseignementProblemeHebdo(study_, *problemeHebdo_, weekInTheYear,
    //             thread_number_, hourInTheYear);
    //         OPT_OptimisationHebdomadaire(options_, problemeHebdo_, adqPatchParams_, writer_);

    //     }
    // }

    for (uint32_t Area = 0; Area < problemeHebdo_->NombreDePays; Area++)
    {
        auto& hourlyResults = problemeHebdo_->ResultatsHoraires[Area];
        const auto& scratchpad = area_list_[Area]->scratchpad[numSpace_];

        for (uint hour = 0; hour < nbHoursInWeek; hour++)
        {
            const bool isHourTriggeredByCsr = problemeHebdo_->adequacyPatchRuntimeData
                                                ->wasCSRTriggeredAtAreaHour(Area, hour);

            const double dtgMrg = scratchpad.dispatchableGenerationMargin[hour];
            const double ens = hourlyResults.ValeursHorairesDeDefaillancePositive[hour];
            const bool areaInside = problemeHebdo_->adequacyPatchRuntimeData->areaMode[Area]
                                    == physicalAreaInsideAdqPatch;
            if (isHourTriggeredByCsr && areaInside)
            {
                hourlyResults.ValeursHorairesDtgMrgCsr[hour] = std::max(0.0, dtgMrg - ens);
                hourlyResults.ValeursHorairesDeDefaillancePositiveCSR[hour] = std::max(0.0,
                                                                                       ens
                                                                                         - dtgMrg);
            }
            else
            {
                // Default value (when the hour is not triggered by CSR)
                hourlyResults.ValeursHorairesDtgMrgCsr[hour] = dtgMrg;
                hourlyResults.ValeursHorairesDeDefaillancePositiveCSR[hour] = ens;
            }
        }
    }
}

// -----------------------------
//  Interpolate Water Values
// -----------------------------
InterpolateWaterValuePostProcessCmd::InterpolateWaterValuePostProcessCmd(
  PROBLEME_HEBDO* problemeHebdo,
  AreaList& areas,
  const Date::Calendar& calendar):
    basePostProcessCommand(problemeHebdo),
    area_list_(areas),
    calendar_(calendar)
{
}

void InterpolateWaterValuePostProcessCmd::execute(const optRuntimeData& opt_runtime_data)
{
    unsigned int hourInYear = opt_runtime_data.hourInTheYear;
    interpolateWaterValue(area_list_, *problemeHebdo_, calendar_, hourInYear);
}

// -----------------------------
//  Hydro Levels Final Update
// -----------------------------
HydroLevelsFinalUpdatePostProcessCmd::HydroLevelsFinalUpdatePostProcessCmd(
  PROBLEME_HEBDO* problemeHebdo,
  AreaList& areas):
    basePostProcessCommand(problemeHebdo),
    area_list_(areas)
{
}

void HydroLevelsFinalUpdatePostProcessCmd::execute(const optRuntimeData&)
{
    updatingWeeklyFinalHydroLevel(area_list_, *problemeHebdo_);
}

// --------------------------------------
//  Curtailment sharing for adq patch
// --------------------------------------
CurtailmentSharingPostProcessCmd::CurtailmentSharingPostProcessCmd(
  const AdqPatchParams& adqPatchParams,
  PROBLEME_HEBDO* problemeHebdo,
  AreaList& areas,
  unsigned int numSpace,
  const OptimizationOptions& solverOptions):
    basePostProcessCommand(problemeHebdo),
    area_list_(areas),
    adqPatchParams_(adqPatchParams),
    numSpace_(numSpace),
    solverOptions_(solverOptions)
{
}

void CurtailmentSharingPostProcessCmd::execute(const optRuntimeData& opt_runtime_data)
{
    unsigned int year = opt_runtime_data.year;
    unsigned int week = opt_runtime_data.week;

    double totalLmrViolation = calculateDensNewAndTotalLmrViolation();
    logs.info() << "[adq-patch] Year:" << year + 1 << " Week:" << week + 1
                << ".Total LMR violation:" << totalLmrViolation;
    const std::set<int> hoursRequiringCurtailmentSharing = getHoursRequiringCurtailmentSharing();

    HourlyCSRProblem hourlyCsrProblem(adqPatchParams_, problemeHebdo_, solverOptions_);

    auto backup = problemeHebdo_->CorrespondanceVarNativesVarOptim;

    auto variableManager = VariableManagerFromProblemHebdo(problemeHebdo_);

    for (uint hour = 0; hour < 1; hour++)
    {
        auto f = problemeHebdo_->ValeursDeNTC[hour].ValeurDuFlux;
        // logs.info() << "[adq-patch] flux Before ADQPTCH:" <<f;// << Aff1;
    }

    // ens bef adqp
    std::vector<std::vector<double>> ENSBef, ENSAfter, ENSRedispatch, SpillBef, SpillAfter,
      SpillRedispatch, dtgMrgBef, dtgMrgAfter, dtgMrgRedispatch, ovCostBef, ovCostAfter,
      ovCostRedispatch;
    // fixedFlows and InitialFlows are unused
    std::vector<std::vector<double>> fixedFlows(nbHoursInWeek,
                                                std::vector<double>(
                                                  problemeHebdo_->NombreDInterconnexions));
    std::vector<std::vector<double>> InitialFlows(nbHoursInWeek,
                                                  std::vector<double>(
                                                    problemeHebdo_->NombreDInterconnexions));

    ENSBef.resize(problemeHebdo_->NombreDePays);
    SpillBef.resize(problemeHebdo_->NombreDePays);
    dtgMrgBef.resize(problemeHebdo_->NombreDePays);
    ovCostBef.resize(problemeHebdo_->NombreDePays);

    ENSAfter.resize(problemeHebdo_->NombreDePays);
    SpillAfter.resize(problemeHebdo_->NombreDePays);
    dtgMrgAfter.resize(problemeHebdo_->NombreDePays);
    ovCostAfter.resize(problemeHebdo_->NombreDePays);
    
    ENSRedispatch.resize(problemeHebdo_->NombreDePays);
    SpillRedispatch.resize(problemeHebdo_->NombreDePays);
    dtgMrgRedispatch.resize(problemeHebdo_->NombreDePays);
    ovCostRedispatch.resize(problemeHebdo_->NombreDePays);

    // const double dtgMrg = scratchpad.dispatchableGenerationMargin[hour];

    bool b = true; // for redispatch success validations

    for (uint32_t area = 0; area < problemeHebdo_->NombreDePays; ++area)
    {
        // logs.info() << area << " with ens / Spill:";
        ENSBef[area] = problemeHebdo_->ResultatsHoraires[area].ValeursHorairesDeDefaillancePositive;
        SpillBef[area] = problemeHebdo_->ResultatsHoraires[area]
                           .ValeursHorairesDeDefaillanceNegative;
        const auto& scratchpad = area_list_[area]->scratchpad[numSpace_];
        dtgMrgBef[area] = std::vector<double>(std::begin(scratchpad.dispatchableGenerationMargin),
                                              std::end(scratchpad.dispatchableGenerationMargin));
        // ovCost[area] = problemHebdo_->ResultatsHoraires[area];
        // dtgMrgBef[area] = scratchpad.dispatchableGenerationMargin.copy();
    }

    double solCostDisp = problemeHebdo_->coutOptimalSolution2[0];
    logs.info() << " optCostDisp : " << solCostDisp;

    // for (uint hourInWeek = 0; hourInWeek < nbHoursInWeek; ++hourInWeek)
    // {
    //     for (uint32_t Interco = 0; Interco < problemeHebdo_->NombreDInterconnexions; ++Interco)
    //     {
    //         auto f = problemeHebdo_->ValeursDeNTC[hourInWeek].ValeurDuFlux[Interco];
    //         InitialFlows[hourInWeek][Interco] = f;
    //     }
    // }

    // for (uint32_t area = 0; area < ENSBef.size(); ++area) {
    //     std::string areaName = problemeHebdo_->NomsDesPays[area];
    //     // std::string areaName = getAreaName(area); // Replace with your method to get area
    //     names for (uint h = 0; h < ENSBef[area].size(); ++h) {
    //         logs.info("dtg bla ", dtgMrg[area].at(h))
    //     }
    // }

    // RUN ADQP
    for (int hourInWeek: hoursRequiringCurtailmentSharing)
    {
        hourlyCsrProblem.setHour(hourInWeek);
        hourlyCsrProblem.run(week, year);
    }


    // for (uint hour = 0; hour < 1; hour++){
    //     auto f = problemeHebdo_->ValeursDeNTC[hour].ValeurDuFlux;
    //     // auto f = problemeHebdo_->ValeursDeNTC[hourInWeek].ValeurDuFlux[Interco];
    //     fixedFlows[hourInWeek][Interco] = f;
    //     logs.info() << "[adq-patch] flux After ADQPTCH:" <<f;// << Aff2;
    // }

    for (uint hour = 0; hour < nbHoursInWeek; ++hour)
    {
        for (uint32_t interco = 0; interco < problemeHebdo_->NombreDInterconnexions; ++interco)
        {
            fixedFlows[hour][interco] = problemeHebdo_->ValeursDeNTC[hour].ValeurDuFlux[interco];
        }
    }

    for (uint32_t area = 0; area < problemeHebdo_->NombreDePays; ++area)
    {
        ENSAfter[area] = problemeHebdo_->ResultatsHoraires[area]
                           .ValeursHorairesDeDefaillancePositive;
        SpillAfter[area] = problemeHebdo_->ResultatsHoraires[area]
                             .ValeursHorairesDeDefaillanceNegative;
    }

    // for (uint32_t area = 0; area < problemeHebdo_->NombreDePays; ++area) {
    //     for (uint h = 0; h < nbHoursInWeek; ++h) {
    //         if (ENSBef[area][h] != ENSAfter[area][h]) {
    //             logs.info() << "[ADQPatch] ENS changed for area=" <<
    //             problemeHebdo_->NomsDesPays[area]
    //                     << " hour=" << h
    //                     << " before=" << ENSBef[area][h]
    //                     << " after=" << ENSAfter[area][h];
    //         }
    //     }
    // }

    // Filtering Affected Areas, i.e, areas with weird case
    std::set<uint32_t> affectedAreas;

    for (uint32_t area = 0; area < problemeHebdo_->NombreDePays; ++area)
    {
        for (uint h = 0; h < nbHoursInWeek; ++h)
        {
            // logs.info() << area << " with ENSBef, ENSAfter / SpillBef, SpillAfter:" ;
            // logs.info() << ENSBef[area][h] << " " << ENSAfter[area][h] << " "
            //             << SpillBef[area][h] << " " << SpillAfter[area][h] << " ";

            // Check if ENS transitioned from 0 to positive
            if (ENSBef[area][h] == 0 && ENSAfter[area][h] > 0)
            {
                affectedAreas.insert(area);
            }

            // Check if Spillage transitioned from 0 to positive
            if (SpillBef[area][h] == 0 && SpillAfter[area][h] > 0)
            {
                affectedAreas.insert(area);
            }
        }
    }

    // HERE WE DISPATCH IN CASES NEEDED
    if (!affectedAreas.empty())
    {
        // // Print the selected affected areas
        // // logs.info() << "Affected Areas:";
        // // for (const auto& area : affectedAreas) {
        // //     logs.info() << "Area " << area;
        // // }

        // accessing old bounds adress
        std::vector<double>& Xmax = problemeHebdo_->ProblemeAResoudre->Xmax;
        std::vector<double>& Xmin = problemeHebdo_->ProblemeAResoudre->Xmin;
        std::vector<double>& X = problemeHebdo_->ProblemeAResoudre->X;
        std::vector<int>& TypeVar = problemeHebdo_->ProblemeAResoudre->TypeDeVariable;

        problemeHebdo_->CorrespondanceVarNativesVarOptim = backup;

        // // try Ali here brute: 
        // int var;
        // double oldValue;
        // for (uint h = 0; h < nbHoursInWeek; ++h)
        // {
        //     for (uint32_t area = 0; area < problemeHebdo_->NombreDePays; ++area)
        //     {
        //             // logs.info() << "[adq-patch] Affected Area loop I "<<area;
        //         var = problemeHebdo_->CorrespondanceVarNativesVarOptim[h]
        //                 .NumeroDeVariableDefaillancePositive[area];
        //         oldValue = ENSAfter[area][h];
        //         Xmax[var] = oldValue; // adjust to force zero if ens is already null
        //         Xmin[var] = 0;
        //         X[var] = oldValue;
        //         TypeVar[var] = VARIABLE_BORNEE_DES_DEUX_COTES;
            
        //     }
        // }




        // nonAffected Areas dispatch has to be fixed, we do this by fixing their ENS to the old one
        // modif1
        // int var;
        // double oldValue;
        // for (uint h = 0; h < nbHoursInWeek; ++h)
        // {
        //     for (uint32_t area = 0; area < problemeHebdo_->NombreDePays; ++area)
        //     {
        //         if (!affectedAreas.contains(area))
        //         {
        //             // logs.info() << "[adq-patch] Affected Area loop I "<<area;
        //             var = problemeHebdo_->CorrespondanceVarNativesVarOptim[h]
        //                     .NumeroDeVariableDefaillancePositive[area];
        //             oldValue = ENSAfter[area][h];
        //             Xmax[var] = oldValue; // adjust to force zero if ens is already null
        //             Xmin[var] = oldValue;
        //             X[var] = oldValue;
        //             TypeVar[var] = VARIABLE_BORNEE_DES_DEUX_COTES;
        //         }
        //     }
        // }

        // // Also, affectedAreas that are exporting cannot have an ENS, i.e, their ENS is zero. ignoring this for now...
        // // int var;
        // // double oldValue;
        // // modif2 - LOCAL MATCHING For free
        // double bilanPays;
        // long pInterco;
        // for (uint h = 0; h < nbHoursInWeek; ++h)
        // {
        //     for (uint32_t area = 0; area < problemeHebdo_->NombreDePays; ++area)
        //     {
        //         if (affectedAreas.contains(area))
        //         {
        //             // compute Balance of area:
        //             bilanPays = 0;
        //             // Export, négative
        //             int exportVal = 0;
        //             pInterco = problemeHebdo_->IndexDebutIntercoOrigine[area];
        //             while (pInterco >= 0)
        //             {
        //                 int origin = problemeHebdo_->PaysOrigineDeLInterconnexion[pInterco];
        //                 int extrem = problemeHebdo_->PaysExtremiteDeLInterconnexion[pInterco];
        //                 if (problemeHebdo_->adequacyPatchRuntimeData->areaMode[origin]
        //                     == physicalAreaInsideAdqPatch
        //                     && problemeHebdo_->adequacyPatchRuntimeData->areaMode[extrem]
        //                         == physicalAreaInsideAdqPatch)
        //                 {   
        //                     exportVal += problemeHebdo_->ValeursDeNTC[h].ValeurDuFlux[pInterco];
        //                     bilanPays += problemeHebdo_->ValeursDeNTC[h].ValeurDuFlux[pInterco];
        //                     // logs.info() << "[adq-patch] Interco Exp
        //                     // "<<problemeHebdo_->ValeursDeNTC[h].ValeurDuFlux[pInterco];
        //                 }
        //                 // else
        //                 //     logs.info() << "[adq-patch] Interco non phys spotted" << origin <<" "<< extrem;

        //                 pInterco = problemeHebdo_->IndexSuivantIntercoOrigine[pInterco];

        //             }
        //             // Import, positive
        //             int importVal = 0;
        //             pInterco = problemeHebdo_->IndexDebutIntercoExtremite[area];
        //             while (pInterco >= 0)
        //             {   
        //                 int origin = problemeHebdo_->PaysOrigineDeLInterconnexion[pInterco];
        //                 int extrem = problemeHebdo_->PaysExtremiteDeLInterconnexion[pInterco];
        //                 if (problemeHebdo_->adequacyPatchRuntimeData->areaMode[origin]
        //                     == physicalAreaInsideAdqPatch
        //                     && problemeHebdo_->adequacyPatchRuntimeData->areaMode[extrem]
        //                         == physicalAreaInsideAdqPatch)
        //                 {   
        //                     importVal += problemeHebdo_->ValeursDeNTC[h].ValeurDuFlux[pInterco];
        //                     bilanPays += problemeHebdo_->ValeursDeNTC[h].ValeurDuFlux[pInterco];
        //                     // logs.info() << "[adq-patch] Interco Imp.
        //                     // "<<problemeHebdo_->ValeursDeNTC[h].ValeurDuFlux[pInterco];
                            
        //                 }
        //                 // else
        //                 //     logs.info() << "[adq-patch] Interco non phys spotted" << origin <<" "<< extrem;
        //                 pInterco = problemeHebdo_->IndexSuivantIntercoExtremite[pInterco];

        //             }
        //             // logs.info() << "[adq-patch] NetPos "<<bilanPays;
        //             // if export bigger than import, i.e, bilanPays is negative.
        //             // an area that is exporting should have ENS <=0.
        //             if (bilanPays < 0.)
        //             {
        //                 if (ENSAfter[area][h] > 0.1){
        //                     logs.info() << "[adq-patch] Imp " << importVal <<" export "<< exportVal;
        //                     logs.info() << "[adq-patch] Positive ENSAfter  " << area <<" "<< h << " " << ENSAfter[area][h];
        //                 }
                            
        //                 var = problemeHebdo_->CorrespondanceVarNativesVarOptim[h]
        //                         .NumeroDeVariableDefaillancePositive[area];
        //                 oldValue = ENSAfter[area][h];
        //                 // Xmax[var] = oldValue; // may be zero ?
        //                 Xmax[var] = oldValue; // may be zero ?
        //                 Xmin[var] = 0.;
        //                 X[var] = oldValue;
        //                 TypeVar[var] = VARIABLE_BORNEE_DES_DEUX_COTES;
        //             }
        //         }
        //     }
        // }

        // FIXING THE FLOW
        // the flow is fixed for every connection
        // modif3 

        for (uint hourInWeek = 0; hourInWeek < nbHoursInWeek;
             ++hourInWeek) // hourInWeek: hoursRequiringCurtailmentSharing)
        {
            for (uint32_t Interco = 0; Interco < problemeHebdo_->NombreDInterconnexions; ++Interco)
            {
                int origin = problemeHebdo_->PaysOrigineDeLInterconnexion[Interco];
                int extrem = problemeHebdo_->PaysExtremiteDeLInterconnexion[Interco];

                if (problemeHebdo_->adequacyPatchRuntimeData->areaMode[origin] == physicalAreaInsideAdqPatch
                     && problemeHebdo_->adequacyPatchRuntimeData->areaMode[extrem] == physicalAreaInsideAdqPatch)
                {
                    int var = variableManager.NTCDirect(Interco, hourInWeek);
                        // if (TypeVar[var] == VARIABLE_BORNEE_DES_DEUX_COTES){    
                    auto f = problemeHebdo_->ValeursDeNTC[hourInWeek].ValeurDuFlux[Interco];
                    if (origin == 44 || extrem == 44) // node zz_flowbased
                        if (41<=hourInWeek and hourInWeek <=43)
                            logs.info() <<origin<<" "<<problemeHebdo_->NomsDesPays[origin]<<" "<<extrem<<" "<<problemeHebdo_->NomsDesPays[extrem]<<" f "<<f<<" :  type "<<TypeVar[var] <<" xmin: "<<Xmin[var]<<"  xmax: "<<Xmax[var];

                    // Xmax[var] = f + 1;// + 0.01 ;// + 10;
                    Xmin[var] = f - 1;// - 0.01;//- 10;
                    // X[var] = f;
                        // }
                    TypeVar[var] = VARIABLE_BORNEE_DES_DEUX_COTES;
                }
                    // TypeVar[var] = VARIABLE_FIXE; // Variable fixe
                    
                // }
            }
        }

        // NEW 
        // Redispatch, calling the solver
        auto optPeriodStringGenerator = createOptPeriodAsString(
          problemeHebdo_->OptimisationAuPasHebdomadaire,
          0,
          problemeHebdo_->weekInTheYear,
          problemeHebdo_->year);
        SimulationTableCsv simTable;
        b = OPT_AppelDuSimplexe(opt_runtime_data.weeklyOptimization.options_.firstOptimOptions,
                                     problemeHebdo_,
                                     0,
                                     PREMIERE_OPTIMISATION,
                                     *optPeriodStringGenerator,
                                     opt_runtime_data.weeklyOptimization.writer_,
                                     simTable);

        if (b && !problemeHebdo_->Expansion && !problemeHebdo_->OptimisationAvecVariablesEntieres)
        {
            runThermalHeuristic(problemeHebdo_);
            b = OPT_AppelDuSimplexe(
              opt_runtime_data.weeklyOptimization.options_.firstOptimOptions,
              problemeHebdo_,
              0,
              DEUXIEME_OPTIMISATION,
              *optPeriodStringGenerator,
              opt_runtime_data.weeklyOptimization.writer_,
              simTable);

        }
        if (!b){
            logs.info() << " redisp, b : "<< b <<" happeend at week "<<week;
        }
        logs.info() << " redisp, b : "<< b <<" happeend at week "<<week;

        // END second sep
        // double solCost = problemeHebdo_->coutOptimalSolution2[numeroDeLIntervalle];
        // return Probleme->coutOptimalSolution2[NumeroDeLIntervalle];

        // } // ENS REDISPATCH //
    } // END REDISPATCH IF SET Affected non empty

    // OUTPUTTING DATA HERE
    double solCostRedisp = problemeHebdo_->coutOptimalSolution2[0];
    logs.info() << " optCostRedisp : "<< solCostRedisp;




    // ===========================================================
    // FileG1 RELATIVE: Write ENS dispatch data into output folder
    // ===========================================================

    // // -- Simulation metadata
    // uint32_t mcy = problemeHebdo_->year;
    // logs.info() << "[adq-patch] MCyear " << mcy;

    // const uint32_t NBHoursInAYear = 364 * 24;

    // --------------------------------------------
    // 1. Prepare CSV buffer in memory
    // --------------------------------------------
    uint32_t mcy = problemeHebdo_->year;
    logs.info() << "[adq-patch] mcY "<<mcy;
    const uint32_t NBHoursInAYear = 364 * 24; // 364
    
    std::ostringstream oss;
    oss << "MCyear\thour\ttimeID\tUtimeID\tarea\tareaName\tENS\tSpill\tDtgMrg\n";

    for (uint32_t area = 0; area < problemeHebdo_->NombreDePays; ++area)
    {
        std::string areaName = problemeHebdo_->NomsDesPays[area];

        for (uint h = 0; h < nbHoursInWeek; ++h)
        {
            if (ENSBef[area][h] > 0  )
            {
                uint32_t timeId = h + week * 168;
                uint32_t uniqueTimeId = NBHoursInAYear * mcy + timeId;

                oss << mcy << "\t"
                    << h << "\t"
                    << timeId << "\t"
                    << uniqueTimeId << "\t"
                    << area << "\t"
                    << areaName << "\t"
                    << std::fixed << std::setprecision(3) << ENSBef[area][h] << "\t"
                    << std::fixed << std::setprecision(3) << SpillBef[area][h] << "\t"
                    << std::fixed << std::setprecision(3) << dtgMrgBef[area][h] << "\n";
            }
        }
    }

    // // --------------------------------------------
    // // 2. Send to Antares result writer
    // // --------------------------------------------

    // // Access the result writer object (not a pointer)
    auto& resultWriter = opt_runtime_data.weeklyOptimization.writer_;
    std::string writeBuffer = oss.str();
    // resultWriter.addEntryFromBuffer(entryPath, writeBuffer);

    // logs.info() << "[adq-patch] Wrote ENSdispatch.csv into study results.";

    // ===========================================================
    // 2. Send to Antares result writer (append mode, in-memory buffer)
    // ===========================================================

    // Static accumulator buffer for all dispatch writes
    static std::ostringstream dispatchBuffer;

    // Append new content to the shared buffer
    dispatchBuffer << oss.str();

    // If we decide to flush at the end of this phase (e.g. last week or post-loop)
    {
        auto& resultWriter = opt_runtime_data.weeklyOptimization.writer_;
        std::filesystem::path entryPath = "intermediateResults/OutputDispatch.csv";

        // Convert the accumulated buffer to string once
        std::string writeBuffer = dispatchBuffer.str();

        // Write it into the results archive
        resultWriter.addEntryFromBuffer(entryPath, writeBuffer);

        logs.info() << "[adq-patch] Appended OutputDispatch.csv into study results.";

        // Optional: clear buffer if you only want a single write per simulation
        // dispatchBuffer.str("");
        // dispatchBuffer.clear();
    }

    // ===========================================================
    // END FileG1 RELATIVE: Write ENS dispatch data into output folder
    // ===========================================================



    // ===========================================================
    // FileG2 RELATIVE: Write another ENS dispatch data into output folder
    // ===========================================================

    // --------------------------------------------
    // 1. Prepare CSV buffer in memory
    // --------------------------------------------
    // uint32_t mcy = problemeHebdo_->year;
    // logs.info() << "[adq-patch][FileG2] mcY " << mcy;
    // const uint32_t NBHoursInAYear = 364 * 24; // 364 days * 24 hours

    std::ostringstream ossG2;
    ossG2 << "MCyear\thour\ttimeID\tUtimeID\tarea\tareaName\tENS\tSpill\tDtgMrg\n";

    for (uint32_t area = 0; area < problemeHebdo_->NombreDePays; ++area)
    {
        std::string areaName = problemeHebdo_->NomsDesPays[area];

        for (uint h = 0; h < nbHoursInWeek; ++h)
        {
            // Example filter (can adjust based on FileG2 semantics)
            if (ENSAfter[area][h] > 0  )
            {
                uint32_t timeId = h + week * 168;
                uint32_t uniqueTimeId = NBHoursInAYear * mcy + timeId;

                ossG2 << mcy << "\t"
                    << h << "\t"
                    << timeId << "\t"
                    << uniqueTimeId << "\t"
                    << area << "\t"
                    << areaName << "\t"
                    << std::fixed << std::setprecision(3) << ENSAfter[area][h] << "\t"
                    << std::fixed << std::setprecision(3) << SpillAfter[area][h] << "\t"
                    << std::fixed << std::setprecision(3) << dtgMrgBef[area][h] << "\n"; // same as dtgMrgBefore 
            }
        }
    }

    // ===========================================================
    // 2. Send to Antares result writer (append mode, in-memory buffer)
    // ===========================================================

    // Static accumulator buffer for all G2 dispatch writes
    static std::ostringstream dispatchBufferG2;

    // Append new content to the shared buffer
    dispatchBufferG2 << ossG2.str();

    // Flush accumulated content into study results (end-of-phase or post-loop)
    {
        auto& resultWriter = opt_runtime_data.weeklyOptimization.writer_;
        std::filesystem::path entryPath = "intermediateResults/OutputADQPatch.csv";

        // Convert accumulated buffer to string
        std::string writeBuffer = dispatchBufferG2.str();

        // Write it into the results archive
        resultWriter.addEntryFromBuffer(entryPath, writeBuffer);

        logs.info() << "[adq-patch][FileG2] Appended OutputADQPatch.csv into study results.";

        // Optional: clear buffer if you only want a single write per simulation
        // dispatchBufferG2.str("");
        // dispatchBufferG2.clear();
    }

    // ===========================================================
    // END FileG2 RELATIVE: Write ENS dispatch data into output folder
    // ===========================================================



    // ===========================================================
    // FileG3 RELATIVE: Write ENS Redispatch data into output folder
    // ===========================================================

    // --------------------------------------------
    // 1. Extract Redispatch data
    // --------------------------------------------
    for (uint32_t area = 0; area < problemeHebdo_->NombreDePays; ++area)
    {
        ENSRedispatch[area] =
            problemeHebdo_->ResultatsHoraires[area].ValeursHorairesDeDefaillancePositive;
        SpillRedispatch[area] =
            problemeHebdo_->ResultatsHoraires[area].ValeursHorairesDeDefaillanceNegative;

        const auto& scratchpad = area_list_[area]->scratchpad[numSpace_];
        dtgMrgRedispatch[area] = std::vector<double>(
            std::begin(scratchpad.dispatchableGenerationMargin),
            std::end(scratchpad.dispatchableGenerationMargin));
    }

    // --------------------------------------------
    // 2. Prepare in-memory CSV buffer
    // --------------------------------------------
    // uint32_t mcy = problemeHebdo_->year;
    // logs.info() << "[adq-patch][FileG3] mcY " << mcy;

    // const uint32_t NBHoursInAYear = 364 * 24; // 364 days * 24 hours

    std::ostringstream ossRedispatch;
    ossRedispatch << "MCyear\thour\ttimeID\tUtimeID\tarea\tareaName\tENS\tSpill\tDtgMrg\n";

    for (uint32_t area = 0; area < problemeHebdo_->NombreDePays; ++area)
    {
        const std::string& areaName = problemeHebdo_->NomsDesPays[area];

        for (uint h = 0; h < nbHoursInWeek; ++h)
        {
            if (ENSRedispatch[area][h] > 0  )
            {
                uint32_t timeId = h + week * 168;
                uint32_t uniqueTimeId = NBHoursInAYear * mcy + timeId;

                ossRedispatch << mcy << "\t"
                            << h << "\t"
                            << timeId << "\t"
                            << uniqueTimeId << "\t"
                            << area << "\t"
                            << areaName << "\t"
                            << std::fixed << std::setprecision(3) << ENSRedispatch[area][h] << "\t"
                            << std::fixed << std::setprecision(3) << SpillRedispatch[area][h] << "\t"
                            << std::fixed << std::setprecision(3) << dtgMrgRedispatch[area][h] << "\n";
            }
        }
    }

    // ===========================================================
    // 3. Send to Antares result writer (append mode, in-memory buffer)
    // ===========================================================

    // Static accumulator buffer for all Redispatch writes
    static std::ostringstream redispatchBuffer;

    // Append new content to the shared buffer
    redispatchBuffer << ossRedispatch.str();

    // Flush accumulated content into study results (end-of-phase or post-loop)
    {
        auto& resultWriter = opt_runtime_data.weeklyOptimization.writer_;
        std::filesystem::path entryPathRedisp = "intermediateResults/OutputRedispatch.csv";

        // Convert accumulated buffer to string
        std::string writeBuffer = redispatchBuffer.str();

        // Write to Antares results archive
        resultWriter.addEntryFromBuffer(entryPathRedisp, writeBuffer);

        logs.info() << "[adq-patch][FileG3] Appended OutputRedispatch.csv into study results.";

        // Optional: clear buffer if you only want a single write per simulation
        // redispatchBuffer.str("");
        // redispatchBuffer.clear();
    }

    // ===========================================================
    // END FileG3 RELATIVE: Write ENS Redispatch data into output folder
    // ===========================================================





    // ===========================================================
    // FileG4 RELATIVE: Compute and write costs + warnings
    // ===========================================================

    // --------------------------------------------
    // 1. Compute total ENS and cost metrics
    // --------------------------------------------
    double sumEnsDisp = 0.0;
    double sumEnsAdqp = 0.0;
    double sumEnsRedisp = 0.0;

    // Sum ENS values for dispatchable and adequacy
    for (uint32_t area = 0; area < problemeHebdo_->NombreDePays; ++area)
    {
        for (uint h = 0; h < nbHoursInWeek; ++h)
        {
            // bool includeArea = std::string(problemeHebdo_->NomsDesPays[area]) < "v";
            // if (includeArea)
            // {
                sumEnsDisp += ENSBef[area][h];
                sumEnsAdqp += ENSAfter[area][h];
                sumEnsRedisp += ENSRedispatch[area][h];
            // }
        }
    }

    double solCostAdqP = solCostDisp + 4000.0 * (sumEnsAdqp - sumEnsDisp);

    // --------------------------------------------
    // 2. Prepare warnings (append mode)
    // --------------------------------------------
    static std::ostringstream warningAccum;

    if (solCostAdqP != solCostDisp)
    {
        warningAccum << "[Redispatch-CostCheck] Diff cost cool: "
                    << "disp = " << solCostDisp << ", adqp = " << solCostAdqP
                    << " (year=" << year << ", week=" << week << ")\n";
        logs.warning() << "[adq-patch][FileG4] Cost difference detected.";
    }

    if (solCostAdqP < solCostDisp - 3)
    {
        warningAccum << "[Redispatch-CostCheck] Unexpectedly low adqpatch cost: "
                    << "disp = " << solCostDisp << ", adqp = " << solCostAdqP
                    << " (year=" << year << ", week=" << week << ")\n";
        logs.warning() << "[adq-patch][FileG4] Unexpectedly low adqpatch cost.";
    }

    if (solCostRedisp < solCostDisp - 3)
    {
        warningAccum << "[Redispatch-CostCheck] Unexpectedly low redispatch cost: "
                    << "disp = " << solCostDisp << ", redisp = " << solCostRedisp
                    << " (year=" << year << ", week=" << week << ")\n";
        logs.warning() << "[adq-patch][FileG4] Unexpectedly low redispatch cost.";
    }

    // --------------------------------------------
    // 3. Prepare cost buffer (append mode)
    // --------------------------------------------
    static std::ostringstream costAccum;

    if (costAccum.tellp() == 0)
    {
        // Write header only once
        costAccum << "Year\tWeek\tSolCostDisp\tSolCostAdqP\tSolCostRedispatch\tSumENSDisp\tSumENSAdqp\tSumENSRedisp\n";
    }

    costAccum << year << "\t"
            << week << "\t"
            << std::fixed << std::setprecision(6)
            << solCostDisp << "\t"
            << solCostAdqP << "\t"
            << solCostRedisp << "\t"
            << sumEnsDisp << "\t"
            << sumEnsAdqp << "\t"
            << sumEnsRedisp << "\n";
            

    // --------------------------------------------
    // 4. Flush buffers to Antares results (append mode)
    // --------------------------------------------

    // (You may move this flush to the end of the main simulation loop)
    {
        auto& resultWriter = opt_runtime_data.weeklyOptimization.writer_;

        // --- Write warnings ---
        std::filesystem::path warningPath = "intermediateResults/warning.txt";
        std::string warningStr = warningAccum.str();
        resultWriter.addEntryFromBuffer(warningPath, warningStr);

        // --- Write cost table ---
        std::filesystem::path costPath = "intermediateResults/Cost.csv";
        std::string costStr = costAccum.str();
        resultWriter.addEntryFromBuffer(costPath, costStr);

        logs.info() << "[adq-patch][FileG4] Appended Cost.csv and warning.txt into study results.";

        // Optional: clear if single-write policy per run
        // warningAccum.str("");
        // warningAccum.clear();
        // costAccum.str("");
        // costAccum.clear();
    }

    // ===========================================================
    // END FileG4 RELATIVE: Compute and write costs + warnings
    // ===========================================================




    // ===========================================================
    // FileG5 RELATIVE: Verify and write flow data (before/after)
    // ===========================================================


    const double tolerance = 0.1;

    // 1. Prepare CSV header
    std::ostringstream ossFlow;
    ossFlow << "MCyear\tWeek\tHour\tInterco\tOrigin\tExtremity\tFlowBefore\tFlowAfter\tDifference\tStatus\n";

    // uint32_t mcy = problemeHebdo_->year;
    auto& noms = problemeHebdo_->NomsDesPays;

    for (uint hour = 0; hour < nbHoursInWeek; ++hour)
    {
        for (uint32_t interco = 0; interco < problemeHebdo_->NombreDInterconnexions; ++interco)
        {
            int origin = problemeHebdo_->PaysOrigineDeLInterconnexion[interco];
            int extrem = problemeHebdo_->PaysExtremiteDeLInterconnexion[interco];
            if (problemeHebdo_->adequacyPatchRuntimeData->areaMode[origin] == physicalAreaInsideAdqPatch
                            && problemeHebdo_->adequacyPatchRuntimeData->areaMode[extrem] == physicalAreaInsideAdqPatch)
            {
                double flowBefore = 0.0;
                double flowAfter = 0.0;

                // If you recorded before ADQ patch flow
                // (if InitialFlows was filled earlier)
                if (hour < InitialFlows.size() && interco < InitialFlows[hour].size())
                    flowBefore = fixedFlows[hour][interco];

                // Current (post-redispatch) flow
                flowAfter = problemeHebdo_->ValeursDeNTC[hour].ValeurDuFlux[interco];

                double diff = std::abs(flowAfter - flowBefore);
                std::string status = (diff <= tolerance) ? "OK" : "CHANGED";

                // Write line
                ossFlow << mcy << "\t" << week << "\t" << hour << "\t" << interco << "\t"
                        << noms[problemeHebdo_->PaysOrigineDeLInterconnexion[interco]] << "\t"
                        << noms[problemeHebdo_->PaysExtremiteDeLInterconnexion[interco]] << "\t"
                        << std::fixed << std::setprecision(3)
                        << flowBefore << "\t" << flowAfter << "\t" << diff << "\t" << status << "\n";

                // Also log warnings if large deviation
                if (diff > tolerance)
                {
                    std::ostringstream warn;
                    warn << "[FlowCheck] Significant flow change at hour=" << hour
                        << ", interco=" << interco
                        << " (" << noms[problemeHebdo_->PaysOrigineDeLInterconnexion[interco]]
                        << "->" << noms[problemeHebdo_->PaysExtremiteDeLInterconnexion[interco]]
                        << "): before=" << flowBefore
                        << ", after=" << flowAfter
                        << ", diff=" << diff;
                    logs.warning() << warn.str();
                    warningAccum << warn.str() << "\n";
                }

            }
        }
    }

    // 2. Append flow info to results
    static std::ostringstream flowBuffer;
    flowBuffer << ossFlow.str();

    {
        auto& resultWriter = opt_runtime_data.weeklyOptimization.writer_;
        std::filesystem::path flowPath = "intermediateResults/FlowCheck.csv";
        std::string writeBuffer = flowBuffer.str();
        resultWriter.addEntryFromBuffer(flowPath, writeBuffer);
        logs.info() << "[adq-patch][FileG5] Appended FlowCheck.csv into study results.";
    }

    // 3. Handle solver failure flag `b`
    if (!b)
    {
        std::ostringstream failMsg;
        failMsg << "[Solver] Redispatch failed (b=false) at week " << week
                << ", year " << year << ". Check feasibility or variable bounds.";
        logs.warning() << failMsg.str();
        warningAccum << failMsg.str() << "\n";
    }

    // 4. Flush updated warnings back to results
    {
        auto& resultWriter = opt_runtime_data.weeklyOptimization.writer_;
        std::filesystem::path warningPath = "intermediateResults/warning.txt";
        std::string warningStr = warningAccum.str();
        resultWriter.addEntryFromBuffer(warningPath, warningStr);
    }




    /* old comment */
    // std::vector<std::vector<double>> finalFlows(nbHoursInWeek,
    // std::vector<double>(problemeHebdo_->NombreDInterconnexions)); for (uint hour = 0; hour <
    // nbHoursInWeek; ++hour){
    //     for (uint32_t interco = 0; interco < problemeHebdo_->NombreDInterconnexions; ++interco){
    //         finalFlows[hour][interco] = problemeHebdo_->ValeursDeNTC[hour].ValeurDuFlux[interco];
    //     }
    // }

    // double tolerance = 1; // should be smaller than your bounds (0.1)
    // for (uint hour = 0; hour < nbHoursInWeek; ++hour){
    //     for (uint32_t interco = 0; interco < problemeHebdo_->NombreDInterconnexions; ++interco){
    //         double expected = fixedFlows[hour][interco];
    //         double finale = finalFlows[hour][interco];
    //         double initial = InitialFlows[hour][interco];
    //         if (std::abs(finale - expected) > tolerance /*&& expected > 1*/){
    //             std::ostringstream msg;
    //             msg << "[Redispatch-FlowCheck] Flow mismatch at hour " << hour
    //                 << ", interco " << interco
    //                 << ": initial " << initial << ", fixed " << expected
    //                 << ", while final " << finale;

    //             logs.warning() << msg.str();  // Log to system
    //             // Also write to file called "warning"
    //             std::ofstream
    //             warningFile("/home/alzoobiali/Desktop/Redispatch/intermediateResults/warning",
    //             std::ios::app);  // Open in append mode if (warningFile.is_open()) {
    //                 warningFile << msg.str() << std::endl;
    //             }
    //         }

    //     }
    // }

    // uint32_t mcy = problemeHebdo_->year;
    // logs.info() << "[adq-patch] mcY "<<mcy;
    // const uint32_t NBHoursInAYear = 364 * 24; // 364
        // fileG1:
        // 1. Define the dump file path in your build/run folder

    // std::string dumpFile =
    // "/home/alzoobiali/Desktop/Redispatch/intermediateResults/ENSdispatch.csv";

    // // 2. Open the file (overwrite or append as you wish)
    // std::ofstream ofsDispatch(dumpFile, std::ios::app /* or std::ios::app */);
    // ofsDispatch << "MCyear\thour\ttimeID\tUtimeID\tarea\tareaName\tENS\tSpill\tDtgMrg\n";
    // for (uint32_t area = 0; area < problemeHebdo_->NombreDePays; ++area) {
    //     std::string areaName = problemeHebdo_->NomsDesPays[area];
    //     // std::string areaName = getAreaName(area); // Replace with your method to get areanames
    //     for (uint h = 0; h < nbHoursInWeek; ++h) {
    //         if (ENSBef[area][h] > 0 && areaName < "v") {
    //             uint32_t timeId = h + week * 168;
    //             uint32_t uniqueTimeId = NBHoursInAYear*mcy + timeId;
    //             ofsDispatch << mcy << "\t"
    //                 << h << "\t"
    //                 << timeId << "\t"
    //                 << uniqueTimeId << "\t"
    //                 << area << "\t"
    //                 << areaName << "\t"
    //                 << std::fixed << std::setprecision(3) << ENSBef[area][h] << "\t"
    //                 << std::fixed << std::setprecision(3) << SpillBef[area][h] << "\t"
    //                 << std::fixed << std::setprecision(3) << dtgMrgBef[area][h] << "\n";
    //         }
    //     }
    // }

    // // 4. Close the file when done
    // ofsDispatch.close();

        // FileG1 RELATIVE:
    // //FileG3
    // // extracting data:
    // for (uint32_t area = 0; area < problemeHebdo_->NombreDePays; ++area){
    //     // logs.info() << area << " with ens / Spill:";
    //     ENSRedispatch[area] =
    //     problemeHebdo_->ResultatsHoraires[area].ValeursHorairesDeDefaillancePositive;
    //     SpillRedispatch[area] =
    //     problemeHebdo_->ResultatsHoraires[area].ValeursHorairesDeDefaillanceNegative; const auto&
    //     scratchpad = area_list_[area]->scratchpad[numSpace_]; dtgMrgRedispatch[area] =
    //     std::vector<double>(std::begin(scratchpad.dispatchableGenerationMargin),std::end(scratchpad.dispatchableGenerationMargin));
    // }

    // // 1. Define the dump file path in your build/run folder
    // std::string dumpFile3 =
    // "/home/alzoobiali/Desktop/Redispatch/intermediateResults/ENSRedispatch.csv";

    // // 2. Open the file (overwrite or append as you wish)
    // std::ofstream ofsRedispatch(dumpFile3, std::ios::app /* or std::ios::app */);
    // ofsRedispatch << "MCyear\thour\ttimeID\tUtimeID\tarea\tareaName\tENS\tSpill\tDtgMrg\n";

    // for (uint32_t area = 0; area < problemeHebdo_->NombreDePays; ++area) {
    //     std::string areaName = problemeHebdo_->NomsDesPays[area];
    //     // std::string areaName = getAreaName(area); // Replace with your method to get area names
    //     for (uint h = 0; h < nbHoursInWeek; ++h) {
    //         if (ENSRedispatch[area][h] > 0 && areaName < "v") {
    //             uint32_t timeId = h + week * 168;
    //             uint32_t uniqueTimeId = NBHoursInAYear*mcy + timeId;
    //             ofsRedispatch << mcy << "\t"
    //                 << h << "\t"
    //                 << timeId << "\t"
    //                 << uniqueTimeId << "\t"
    //                 << area << "\t"
    //                 << areaName << "\t"
    //                 << std::fixed << std::setprecision(3) << ENSRedispatch[area][h] << "\t"
    //                 << std::fixed << std::setprecision(3) << SpillRedispatch[area][h] << "\t"
    //                 << std::fixed << std::setprecision(3) << dtgMrgRedispatch[area][h] << "\n";
    //         }
    //     }
    // }

    // // 4. Close the file when done
    // ofsRedispatch.close();








    // double sumEnsDisp = 0.0;
    // double sumEnsAdqp = 0.0;
    // for (uint32_t area = 0; area < problemeHebdo_->NombreDePays; ++area) {
    //     for (uint h = 0; h < nbHoursInWeek; ++h) {
    //         bool b = std::string(problemeHebdo_->NomsDesPays[area]) < std::string("v");
    //         if (b){
    //             sumEnsDisp += ENSBef[area][h];
    //             sumEnsAdqp += ENSAfter[area][h];
    //         }
    //     }
    // }
    // double solCostAdqP = solCostDisp + 4000.0 * (sumEnsAdqp - sumEnsDisp);
    // if (solCostAdqP != solCostDisp){
    //     std::ostringstream msg;

    //     msg << "[Redispatch-CostCheck] Diff cost cool: "
    //         << "disp = " << solCostDisp << ", adqp = " << solCostAdqP;

    //     logs.warning() << msg.str();  // Log to system
    //     std::ofstream
    //     warningFile("/home/alzoobiali/Desktop/Redispatch/intermediateResults/warning",
    //     std::ios::app); if (warningFile.is_open()) {
    //         warningFile << msg.str() << std::endl;
    //     }
    // }
    // std::string dumpFileCost =
    // "/home/alzoobiali/Desktop/Redispatch/intermediateResults/Cost.csv";
    //  // 2. Open the file (overwrite or append as you wish)
    // std::ofstream ofsCost(dumpFileCost, std::ios::app);
    // ofsCost << "MCyear\thour\ttimeID\tUtimeID\tarea\tareaName\tENS\tSpill\tDtgMrg\n";
    // ofsCost << std::fixed << std::setprecision(15);  // Use enough precision to capture full
    // double value;

    // ofsCost << solCostDisp << "\t" << solCostRedisp << "\n";
    // ofsCost << year << "\t"
    //     << week << "\t"
    //     << solCostDisp << "\t"
    //     << solCostAdqP << "\t"
    //     << solCostRedisp << "\n";
    // ofsCost.close();
    // // Write costs to file

    // // Check for suspicious cost reduction
    // if (solCostAdqP < solCostDisp - 3) {
    //     std::ostringstream msg;
    //     msg << "[Redispatch-CostCheck] Unexpectedly low adqpatch cost: "
    //         << "disp = " << solCostDisp << ", adqp = " << solCostAdqP;

    //     logs.warning() << msg.str();  // Log to system

    //     // Append to warning file
    //     std::ofstream
    //     warningFile("/home/alzoobiali/Desktop/Redispatch/intermediateResults/warning",
    //     std::ios::app); if (warningFile.is_open()) {
    //         warningFile << msg.str() << std::endl;
    //     }
    // }
    // if (solCostRedisp < solCostDisp - 3) {
    //     std::ostringstream msg;
    //     msg << "[Redispatch-CostCheck] Unexpectedly low redispatch cost: "
    //         << "disp = " << solCostDisp << ", redisp = " << solCostRedisp;

    //     logs.warning() << msg.str();  // Log to system

    //     // Append to warning file
    //     std::ofstream
    //     warningFile("/home/alzoobiali/Desktop/Redispatch/intermediateResults/warning",
    //     std::ios::app); if (warningFile.is_open()) {
    //         warningFile << msg.str() << std::endl;
    //     }
    // }

    //
    

} // END CSR

double CurtailmentSharingPostProcessCmd::calculateDensNewAndTotalLmrViolation()
{
    double totalLmrViolation = 0.0;

    for (uint32_t Area = 0; Area < problemeHebdo_->NombreDePays; Area++)
    {
        if (problemeHebdo_->adequacyPatchRuntimeData->areaMode[Area] == physicalAreaInsideAdqPatch)
        {
            for (uint hour = 0; hour < nbHoursInWeek; hour++)
            {
                const auto [netPositionInit, densNew, totalNodeBalance] = calculateAreaFlowBalance(
                  problemeHebdo_,
                  adqPatchParams_.setToZeroOutsideInsideLinks,
                  Area,
                  hour);
                // adjust densNew according to the new specification/request by ELIA
                /* DENS_new (node A) = max [ 0; ENS_init (node A) + net_position_init (node A)
                                        + ? flows (node 1 -> node A) - DTG.MRG(node A)] */
                const auto& scratchpad = area_list_[Area]->scratchpad[numSpace_];
                double dtgMrg = scratchpad.dispatchableGenerationMargin[hour];
                // write down densNew values for all the hours
                problemeHebdo_->ResultatsHoraires[Area].ValeursHorairesDENS[hour] = std::max(
                  0.0,
                  densNew);
                // check LMR violations
                totalLmrViolation += LmrViolationAreaHour(
                  problemeHebdo_,
                  totalNodeBalance,
                  adqPatchParams_.curtailmentSharing.thresholdDisplayViolations,
                  Area,
                  hour);
            }
        }
    }
    return totalLmrViolation;
}

std::set<int> CurtailmentSharingPostProcessCmd::getHoursRequiringCurtailmentSharing() const
{
    const auto sumENS = calculateENSoverAllAreasForEachHour();
    return identifyHoursForCurtailmentSharing(sumENS);
}

std::set<int> CurtailmentSharingPostProcessCmd::identifyHoursForCurtailmentSharing(
  const std::vector<double>& sumENS) const
{
    const double threshold = adqPatchParams_.curtailmentSharing.thresholdRun;
    std::set<int> triggerCsrSet;
    for (uint i = 0; i < nbHoursInWeek; ++i)
    {
        if (sumENS[i] > threshold)
        {
            triggerCsrSet.insert(i);
        }
    }
    logs.debug() << "number of triggered hours: " << triggerCsrSet.size();
    return triggerCsrSet;
}

std::vector<double> CurtailmentSharingPostProcessCmd::calculateENSoverAllAreasForEachHour() const
{
    std::vector<double> sumENS(nbHoursInWeek, 0.0);
    for (uint32_t area = 0; area < problemeHebdo_->NombreDePays; ++area)
    {
        if (problemeHebdo_->adequacyPatchRuntimeData->areaMode[area]
            == Data::AdequacyPatch::physicalAreaInsideAdqPatch)
        {
            const std::vector<double>& ENS = problemeHebdo_->ResultatsHoraires[area]
                                               .ValeursHorairesDeDefaillancePositive;
            for (uint h = 0; h < nbHoursInWeek; ++h)
            {
                sumENS[h] += ENS[h];
            }
        }
    }
    return sumENS;
}

} // namespace Antares::Solver::Simulation

// OPT_OptimisationHebdomadaire(opt_runtime_data.weeklyOptimization.options_,
//                              problemeHebdo_,
//                              opt_runtime_data.weeklyOptimization.writer_,
//                              opt_runtime_data.weeklyOptimization.simulationObserver_);

// opt_runtime_data.weeklyOptimization.solve();

// opt_runtime_data.weeklyOptimization.solve();

// for (auto& cnxn: opt_runtime_data.weeklyOptimization.problemeHebdo_->ValeursDeNTC){
//         logs.info() << "[adq-patch] Hello ValeurDeFlux AFTER adq is:"<<cnxn.ValeurDuFlux;
//         cnxn.ValeurDeNTCOrigineVersExtremite = cnxn.ValeurDuFlux;
//         cnxn.ValeurDeNTCExtremiteVersOrigine = cnxn.ValeurDuFlux;
// }
// for (auto& cnxn: opt_runtime_data.weeklyOptimization.problemeHebdo_->ValeursDeNTC){
//     for (auto& v: cnxn.ValeurDeNTCOrigineVersExtremite){
//         v = v + 1;
//     }
//     for (auto& v: cnxn.ValeurDeNTCExtremiteVersOrigine){
//         v = v + 1;
//     }
// }

// // REDISPATCH
// for (int hourInWeek: hoursRequiringCurtailmentSharing){
//     for (uint32_t Interco = 0; Interco < problemeHebdo_->NombreDInterconnexions; ++Interco){
//         int var = variableManager.NTCDirect(Interco, hourInWeek);
//         auto& Xmax = problemeHebdo_->ProblemeAResoudre.get()->Xmax[Interco];// .Xmax[var] =
//         1; auto& Xmin = problemeHebdo_->ProblemeAResoudre.get()->Xmin[Interco];// .Xmax[var]
//         = 1;

//         auto f = problemeHebdo_->ValeursDeNTC[Interco].ValeurDuFlux[Interco];//
//         ->ValeurDeNTCOrigineVersExtremite[Interco].ValeurDeFlux; Xmax = f + 1; Xmin = f - 1;
//         // auto c = b[Interco].ValeurDuFlux;

//     }
//     logs.info() << "[adq-patch] Hello NTCs";

// }

// // REDISPATCH OLD Flow cons
// for (int hourInWeek: hoursRequiringCurtailmentSharing){
//     for (uint32_t Interco = 0; Interco < problemeHebdo_->NombreDInterconnexions; ++Interco){
//         // int var = variableManager.NTCDirect(Interco, hourInWeek);
//         auto& Xmax = problemeHebdo_->ProblemeAResoudre.get()->Xmax[Interco];// .Xmax[var] =
//         1; auto& Xmin = problemeHebdo_->ProblemeAResoudre.get()->Xmin[Interco];// .Xmax[var]
//         = 1;

//         auto f = problemeHebdo_->ValeursDeNTC[Interco].ValeurDuFlux[Interco];//
//         ->ValeurDeNTCOrigineVersExtremite[Interco].ValeurDeFlux; Xmax = f + 1; Xmin = f - 1;
//         // auto c = b[Interco].ValeurDuFlux;

//     }
//     logs.info() << "[adq-patch] Hello NTCs";

// }

// auto variableManager = VariableManagerFromProblemHebdo(problemeHebdo);
// for (int pdtHebdo = PremierPdtDeLIntervalle, pdtJour = 0; pdtHebdo < DernierPdtDeLIntervalle;
//      pdtHebdo++, pdtJour++)
// {
//     VALEURS_DE_NTC_ET_RESISTANCES& ValeursDeNTC = problemeHebdo->ValeursDeNTC[pdtHebdo];

//     for (uint32_t interco = 0; interco < problemeHebdo->NombreDInterconnexions; interco++)
//     {
//         int var = variableManager.NTCDirect(interco, pdtJour);
//         const COUTS_DE_TRANSPORT& CoutDeTransport = problemeHebdo->CoutDeTransport[interco];

//         Xmax[var] = ValeursDeNTC.ValeurDeNTCOrigineVersExtremite[interco];
//         Xmin[var] = -(ValeursDeNTC.ValeurDeNTCExtremiteVersOrigine[interco]);

//         if (std::isinf(Xmax[var]) && Xmax[var] > 0)
//         {

//    logs.info()  << "After ADQP::";
// for (uint32_t area = 0; area < problemeHebdo_->NombreDePays; ++area)
// {
//     // ens bef adqp
//     std::vector<double> ENSAfter =
//     problemeHebdo_->ResultatsHoraires[area].ValeursHorairesDeDefaillancePositive;
//     std::vector<double> SpillAfter =
//     problemeHebdo_->ResultatsHoraires[area].ValeursHorairesDeDefaillanceNegative;

//     if (problemeHebdo_->adequacyPatchRuntimeData->areaMode[area] ==
//     Data::AdequacyPatch::physicalAreaInsideAdqPatch){
//         logs.info() << area << "with ens / Spill:";
//         for (uint h = 0; h < 5; ++h)
//         {
//             logs.info() <<  " :" << ENSAfter[h] <<" "<< SpillAfter[h];
//         }
//     }
// }

// // After the ADQP ran, we check if there is apparition de l'ENS, Spillage qq part
// for (uint i = 0; i < nbHoursInWeek; ++i)
// {
//     int ENSBeforeAdqp = 0; // Adqp stands for Adequacy Patch
//     int ENSAfterAdqp = 0;
//     int SpillageBeforeAdqp = 0;
//     int SpillageAfterAdqp = 0;

// }



    // double bilanPays;
    // long pInterco;
    // for (uint h = 0; h < nbHoursInWeek; ++h)
    // {
    //     for (uint32_t area = 0; area < problemeHebdo_->NombreDePays; ++area)
    //     {
    //         // compute Balance of area:
    //         bilanPays = 0;
    //             // Export, négative
    //             pInterco = problemeHebdo_->IndexDebutIntercoOrigine[area];
    //             while (pInterco >= 0)
    //             {   
    //                 bilanPays += problemeHebdo_->ValeursDeNTC[h].ValeurDuFlux[pInterco];
    //                 pInterco = problemeHebdo_->IndexSuivantIntercoOrigine[pInterco];
    //             }
    //             // Import, positive
    //             pInterco = problemeHebdo_->IndexDebutIntercoExtremite[area];
    //             while (pInterco >= 0)
    //             {   
    //                 bilanPays += problemeHebdo_->ValeursDeNTC[h].ValeurDuFlux[pInterco];
    //                 pInterco = problemeHebdo_->IndexSuivantIntercoExtremite[pInterco];
    //             }

    //             if (bilanPays < 0.)
    //             { // exporting ?