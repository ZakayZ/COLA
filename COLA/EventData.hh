/**
 * Copyright (c) 2024-2026 Alexandr Svetlichnyi, Savva Savenkov, Artemii Novikov
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
 * associated documentation files (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge, publish, distribute,
 * sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
 * NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef COLA_EVENTDATA_HH
#define COLA_EVENTDATA_HH

#include "LorentzVector.hh"

#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace cola {
  using LorentzVector = LorentzVectorImpl<double>;

  /** A typedef representing mass and charge of a nucleon.
   */
  using AZ = std::pair<uint16_t, uint16_t>;

  /** PDG code to AZ converter.
   *  **WARNING:** this function is intended to process heavy ions PDG codes.
   *  @param pdgCode PDG code of the ion.
   *  @return AZ of the ion.
   */
  constexpr AZ PdgToAZ(int pdgCode) {
    switch (pdgCode) {
      case 2112:
        return {1, 0};
      case 2212:
        return {1, 1};
      default: {
        AZ data = {0, 0};
        pdgCode /= 10;
        for (int i = 0; i < 3; i++) {
          data.first += pdgCode % 10 * static_cast<uint16_t>(std::pow(10, i));
          pdgCode /= 10;
        }
        for (int i = 0; i < 3; i++) {
          data.second += pdgCode % 10 * static_cast<uint16_t>(std::pow(10, i));
          pdgCode /= 10;
        }
        return data;
      }
    }
  }

  /** AZ to PDG code converter.
   *  @param data AZ of the ion.
   *  @return PDG code of the ion
   */
  constexpr int AZToPdg(AZ data) {
    if (data.first == 1 && data.second == 0) {
      return 2112;
    }
    if (data.first == 1 && data.second == 1) {
      return 2212;
    }
    return 1000000000 + data.first * 10 + data.second * 10000;
  }

  /** \defgroup Data Data Classes and supporting methods.
   * @{
   */

  /** Particle class by generator output.
   *  This enum represents various outcomes of a generator event for every particle.
   */
  enum class ParticleClass : char {
    kProduced,    /**< A particle that was not present in the starting nuclei. */
    kElasticA,    /**< A particle that was present in the projectile nucleus and has experienced only elastic
                   * interactions.
                   */
    kElasticB,    /**< A particle that was present in the target nucleus and has experienced only elastic interactions.
                   */
    kNonelasticA, /**< A particle that was present in the projectile nucleus and has experienced at least one
                    non-elastic interaction. */
    kNonelasticB, /**< A particle that was present in the target nucleus and has experienced at least one
                    non-elastic interaction. */
    kSpectatorA,  /**< A particle that was present in the projectile nucleus and hasn't experienced any interactions.
                   */
    kSpectatorB   /**< A particle that was present in the projectile nucleus and hasn't experienced any interactions.
                   */
  };

  /** Particle data.
   *  A structure representing data about a single particle
   */
  struct Particle {
    AZ GetAZ() const;

    LorentzVector position; /**< Position <t, x, y, z> vector. */

    LorentzVector momentum; /**< Momentum <e, x, y, z> vector. */

    int pdgCode;          /**< PDG code of the particle. */
    ParticleClass pClass; /**< Data about particle origin. See ParticleClass for more info.*/
  };

  /**
   * Convenient typedef for Particle vector.
   */
  using EventParticles = std::vector<Particle>;

  /** Initial state data.
   *  This structure contains data about initial state of any given event.
   */
  struct EventIniState {
    int pdgCodeA; /**< PDG code of the projectile. */
    int pdgCodeB; /**< PDG code of the target. */

    double pZA;    /** Axial momentum of the projectile */
    double pZB;    /** Axial momentum of the target */
    double energy; /** Incidental energy of the event. Depending on pZB being zero, this is either \f$E/A\f$ of
                      target or \f$\sqrt{s_{NN}}\f$. */

    float sectNN; /** Nucleon-Nucleon cross section from generator. */
    float b;      /** Impact parameter of the event. */

    int nColl;   /** Diagnostic. Total number of collisions. */
    int nCollPP; /** Diagnostic. Number of proton-proton. */
    int nCollPN; /** Diagnostic. Number of proton-neutron collisions. */
    int nCollNN; /** Diagnostic. Number of neutron-neutron collisions. */
    int nPart;   /** Diagnostic. Total number of participants. */
    int nPartA;  /** Diagnostic. Number of participants from the projectile nucleus. */
    int nPartB;  /** Diagnostic. Number of participants from the target nucleus. */

    float phiRotA;   /** Diagnostic. Polar angle \f$\phi\f$ of rotation of the projectile nucleon. */
    float thetaRotA; /** Diagnostic. Polar angle \f&\Theta\f$ of rotation of the projectile nucleon. */
    float phiRotB;   /** Diagnostic. Polar angle \f$\phi\f$ of rotation of the target nucleon. */
    float thetaRotB; /** Diagnostic. Polar angle \f$\Theta\f$ of rotation of the target nucleon. */

    EventParticles iniStateParticles; /** The array of all Particles just before the event. */
  };

  /** A structure combining EventIniState and EventParticles of the event.
   */
  struct EventData {
    EventIniState iniState;
    EventParticles particles;
  };

  /** @}
   */

}  // namespace cola

#endif  // COLA_EVENTDATA_HH
