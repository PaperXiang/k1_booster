#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <string>
#include <vector>

#include "formation.h"
#include "utils/math.h"

namespace {

constexpr double kFieldMargin = 0.5;
constexpr double kPenaltyMargin = 0.25;
constexpr double kTolerance = 1e-6;

FormationInput makeThreeVersusThreeInput(const std::string &scene, const Point2D &ballPosition) {
    FormationInput input;
    input.scene = scene;
    input.fd = FD_ADULTSIZE;
    input.ballPos = ballPosition;
    input.ballValid = true;
    input.playerIds = {1, 2};
    input.playerPoses = {{-1.0, -1.0, 0.0}, {-1.0, 1.0, 0.0}};
    input.keepAwayDist = 1.5;
    return input;
}

void expectInsideField(const FormationInput &input, const Pose2D &pose) {
    EXPECT_LE(pose.x, input.fd.length / 2.0 - kFieldMargin + kTolerance);
    EXPECT_GE(pose.x, -input.fd.length / 2.0 + kFieldMargin - kTolerance);
    EXPECT_LE(pose.y, input.fd.width / 2.0 - kFieldMargin + kTolerance);
    EXPECT_GE(pose.y, -input.fd.width / 2.0 + kFieldMargin - kTolerance);
}

void expectOutsideOwnPenaltyArea(const FormationInput &input, const Pose2D &pose) {
    const double penaltyFrontX = -input.fd.length / 2.0 + input.fd.penaltyAreaLength + kPenaltyMargin;
    const double penaltyHalfWidth = input.fd.penaltyAreaWidth / 2.0 + kPenaltyMargin;
    const bool insideReservedPenaltyArea = pose.x < penaltyFrontX - kTolerance
        && std::fabs(pose.y) < penaltyHalfWidth - kTolerance;
    EXPECT_FALSE(insideReservedPenaltyArea);
}

TEST(FormationPlannerTest, UsesDeterministicIdMappingForThreeVersusThreeKickoff) {
    FormationInput input = makeThreeVersusThreeInput("kickoff_attack", {0.0, 0.0});

    input.selfId = 1;
    const FormationResult lowerIdResult = FormationPlanner::assign(input, false);
    input.selfId = 2;
    const FormationResult higherIdResult = FormationPlanner::assign(input, false);

    ASSERT_TRUE(lowerIdResult.valid);
    ASSERT_TRUE(higherIdResult.valid);
    EXPECT_EQ(lowerIdResult.slotName, "shooter");
    EXPECT_EQ(higherIdResult.slotName, "passer");
    EXPECT_NE(lowerIdResult.slotName, higherIdResult.slotName);
}

TEST(FormationPlannerTest, KeepsFreeKickSlotsLegalNearFieldBoundaries) {
    const std::array<Point2D, 6> ballPositions{{
        {FD_ADULTSIZE.length / 2.0 - 0.1, 0.0},
        {-FD_ADULTSIZE.length / 2.0 + 0.1, 0.0},
        {0.0, FD_ADULTSIZE.width / 2.0 - 0.1},
        {0.0, -FD_ADULTSIZE.width / 2.0 + 0.1},
        {-FD_ADULTSIZE.length / 2.0 + 0.2, FD_ADULTSIZE.width / 2.0 - 0.2},
        {-FD_ADULTSIZE.length / 2.0 + FD_ADULTSIZE.penaltyAreaLength, 0.0},
    }};

    for (const std::string &scene : {"freekick_attack", "freekick_defense"}) {
        for (const Point2D &ballPosition : ballPositions) {
            const FormationInput input = makeThreeVersusThreeInput(scene, ballPosition);
            const std::vector<FormationSlot> slots = FormationPlanner::slotsForScene(input);

            ASSERT_EQ(slots.size(), 2U) << "scene=" << scene;
            for (const FormationSlot &slot : slots) {
                expectInsideField(input, slot.pose);
                EXPECT_GE(norm(slot.pose.x - input.ballPos.x, slot.pose.y - input.ballPos.y),
                    input.keepAwayDist - kTolerance);
                if (scene == "freekick_defense") {
                    expectOutsideOwnPenaltyArea(input, slot.pose);
                }
            }
        }
    }
}

TEST(FormationPlannerTest, KeepsKickoffDefenseOutsideCenterCircleAndOwnPenaltyArea) {
    const FormationInput input = makeThreeVersusThreeInput("kickoff_defense", {0.0, 0.0});
    const std::vector<FormationSlot> slots = FormationPlanner::slotsForScene(input);

    ASSERT_EQ(slots.size(), 2U);
    for (const FormationSlot &slot : slots) {
        expectInsideField(input, slot.pose);
        expectOutsideOwnPenaltyArea(input, slot.pose);
        EXPECT_LE(slot.pose.x, -0.3 + kTolerance);
        EXPECT_GE(norm(slot.pose.x, slot.pose.y), input.fd.circleRadius + 0.3 - kTolerance);
    }
}

} // namespace
