#ifndef AGENTS_H
#define AGENTS_H

#define _USE_MATH_DEFINES
/// code to make agents
#include <vector>
#include <cassert>
#include <algorithm>
#include <iostream>

#include "parameters.h"
#include "landscape.h"

// EtaCRW=0.7 #the weight of the CRW component in the BCRW used to model the Indiv movement
// StpSize_ind=7 #Mean step lengths of individuals;
// StpStd_ind=5 # Sandard deviations of step lengths of individuals
// Kappa_ind=3 #oncentration parameters of von Mises directional distributions used for individuals' movement

// Agent class
struct Population {
public:
    Population() :
        nAgents (300),
        coordX (nAgents, 50.0),
        coordY (nAgents, 50.0),
        energy (nAgents, 0.000001),
        // one trait
        trait(nAgents, 0.0),
        counter(nAgents, 0)

    {}
    ~Population() {}

    int nAgents;
    std::vector<double> coordX;
    std::vector<double> coordY;
    std::vector<double> energy;
    std::vector<double> trait;
    std::vector<int> counter;

    // funs for pop
    void setTrait ();
    void initPos(Resources food);
    void move(Resources food);
    void normaliseIntake();
    void Reproduce();
};

void Population::initPos(Resources food) {
    for (size_t i = 0; i < static_cast<size_t>(nAgents); i++) {
        coordX[i] = gsl_rng_uniform(r) * food.dSize;
        coordY[i] = gsl_rng_uniform(r) * food.dSize;
    }
}

void Population::setTrait() {
    for (size_t i = 0; i < static_cast<size_t>(nAgents); i++) {
        trait[i] = gsl_ran_gamma(r, 1.0, 1.0);
    }

    // scale trait
    sort(trait.begin(), trait.end());
    for (size_t i = 0; i < static_cast<size_t>(nAgents); i++) {
        trait[i] = trait[i] / trait[nAgents - 1];
    }

}

void Population::move(Resources food) {

    double heading;
    double landsize = food.dSize;

    for(size_t i = 0; i < static_cast<size_t>(nAgents); i++) {
        double stepSize = gsl_ran_gamma(r, indivStepSize, indivStepSizeSd);
        // pay cost
        energy[i] -= (moveCost * stepSize);

        // get heading checking for counter
        if (counter[i] > 0) {
            heading = etaArs * gsl_ran_gaussian(r, 1.0);
            counter[i] -- ;
        } else {
            heading = etaCrw * gsl_ran_gaussian(r, 1.0);
        }

        // get radians
        heading = heading * M_PI / 180.0;

        // figure out the next position
        coordX[i] = coordX[i] + (stepSize * cos(heading));
        coordY[i] = coordY[i] + (stepSize * sin(heading));

        // make the move on the wrapped landscape
        coordX[i] = fmod(landsize + coordX[i], landsize);

        coordY[i] = fmod(landsize + coordY[i], landsize);
    }
}

std::vector<int> findNearItems(size_t individual, Resources &food, Population &pop){
    // search nearest item only if any are available
    std::vector<int> itemID;

    if (food.nAvailable > 0) {
        std::vector<value> nearItems;
        point currentLoc = point(pop.coordX[individual], pop.coordY[individual]);
        box bbox(point(pop.coordX[individual] - range,
            pop.coordY[individual] - range),
            point(pop.coordX[individual] + range, pop.coordY[individual] + range));

        food.rtree.query(
                    bgi::within(bbox) &&
                    bgi::satisfies([&](value const& v) {return bg::distance(v.first, currentLoc) < 2;}),
                    std::back_inserter(nearItems));

        for(size_t i = 0; i < nearItems.size(); i++){
            itemID.push_back(nearItems[i].second); // store item ids
        }
    }
    return itemID;
}

void forage(size_t individual, Resources &food, Population &pop){
    // find nearest item ids
    std::vector<int> theseItems = findNearItems(individual, food, pop);

    // check near items count
    if(theseItems.size() > 0) {
        // which to pick
        int thisItem = -1;

        // now check them
        for (size_t i = 0; i < theseItems.size(); i++){
            if(food.counter[theseItems[i]] == 0) {
                thisItem = theseItems[i]; // if available pick this item

                // also query the probability of switching movement mode
                if(gsl_ran_bernoulli(r, pop.trait[individual]) == 1) {
                    pop.counter[individual] = searchTime;
                }

                break;
            }
        }

        // if item available
        if (thisItem > -1) {
            pop.energy[individual] += 1.0;
            food.counter[thisItem] = regenTime;
        }
    }
}

/// minor function to normalise vector
void Population::normaliseIntake() {
    // sort vec fitness
    std::vector<double> vecFitness = energy;
    std::sort(vecFitness.begin(), vecFitness.end());

    // get min and max fitness
    double minFitness = vecFitness[0];

    // add abs min to all to remove negatives
    for(size_t i = 0; i < static_cast<size_t>(nAgents); i++) {
        energy[i] += std::fabs(minFitness);
    }

    // resort to get new max
    vecFitness = energy;
    double maxFitness = vecFitness[vecFitness.size()-1];

    // rescale
    for(size_t i = 0; i < static_cast<size_t>(nAgents); i++) {
        energy[i] = energy[i] / maxFitness;
    }

}

void Population::Reproduce() {
    //normalise intake
    normaliseIntake();

    // set up weighted lottery
    std::discrete_distribution<> weightedLottery(energy.begin(), energy.end());

    // get parent trait based on weighted lottery
    std::vector<double> newTrait;
    for (size_t a = 0; static_cast<int>(a) < nAgents; a++) {
        newTrait.push_back(
                    trait[static_cast<size_t>(weightedLottery(rng))]);
    }
    // reset counter
    assert(newTrait.size() == trait.size() && "traits different size");
    counter = std::vector<int> (nAgents);
    assert(counter.size() == nAgents && "counter size wrong");

    // mutate trait
    for (size_t a = 0; static_cast<int>(a) < nAgents; a++) {
        if (gsl_ran_bernoulli(r, mProb) == 1) {
            newTrait[a] += gsl_ran_cauchy(r, static_cast<double>(mShift));

            if (newTrait[a] >= 1.0) {
                newTrait[a] = 0.99;
            }
            if (newTrait[a] <= 0.0) {
                newTrait[a] = 0.00001;
            }
        }
    }

    // swap vectors
    std::swap(trait, newTrait);
    newTrait.clear();


}

#endif // AGENTS_H
