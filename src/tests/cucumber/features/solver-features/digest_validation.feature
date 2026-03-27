  Feature: digest validation

  @fast @digest
  Scenario: TP2_Viz - Verify digest matches reference
    Given the solver study path is "Antares_Simulator_Tests_NR/TP2_Viz"
    When I run antares simulator
    Then the simulation succeeds
    And the digest matches the reference