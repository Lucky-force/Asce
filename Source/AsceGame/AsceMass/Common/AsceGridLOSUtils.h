// AsceGridLOSUtils.h
#pragma once

#include "CoreMinimal.h"
#include "Math/Vector2D.h"

/**
 * 网格 LOS 辅助（流场 RunLOSPass / 视野 ComputeVisionForCell / goal-LOS overlay 共用）：
 * 4 邻方向表 + "障碍格朝向来源方向的近边两顶点"计算（两个模块原本各存一份拷贝，抽到这里防漂移）。
 */
namespace AsceGrid
{
	/** 4 邻方向（顺序：上、右、下、左） */
	struct FDir4 { int32 DX; int32 DY; };

	inline constexpr FDir4 Dirs4[4] = { {0, 1}, {1, 0}, {0, -1}, {-1, 0} };

	/** 障碍格 Obstacle 朝向"来源方向"Dir 的近边上两个顶点：
	 *  VertexForPerp1 对应"Perp1 一侧开口"（Dir 的 +垂直轴一侧）用的顶点，
	 *  VertexForPerp2 对应"Perp2 一侧开口"（-垂直轴一侧）用的顶点。
	 *  （Dir 是上下时近边为水平边，Perp=±X；Dir 是左右时近边为竖直边，Perp=±Y。） */
	FORCEINLINE void GetNearEdgeVertices(const FIntVector2& Obstacle, const FDir4& Dir,
		FVector2D& OutVertexForPerp1, FVector2D& OutVertexForPerp2)
	{
		if (Dir.DX == 0)
		{
			// Dir 是上/下：近边是水平边，Perp1=+X 侧，Perp2=-X 侧
			const float Y = (Dir.DY > 0) ? static_cast<float>(Obstacle.Y) : static_cast<float>(Obstacle.Y + 1);
			OutVertexForPerp1 = FVector2D(Obstacle.X + 1, Y);
			OutVertexForPerp2 = FVector2D(Obstacle.X, Y);
		}
		else
		{
			// Dir 是左/右：近边是竖直边，Perp1=+Y 侧，Perp2=-Y 侧
			const float X = (Dir.DX > 0) ? static_cast<float>(Obstacle.X) : static_cast<float>(Obstacle.X + 1);
			OutVertexForPerp1 = FVector2D(X, Obstacle.Y + 1);
			OutVertexForPerp2 = FVector2D(X, Obstacle.Y);
		}
	}
}
