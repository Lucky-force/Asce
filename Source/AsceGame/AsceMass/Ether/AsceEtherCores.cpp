// AsceEtherCores.cpp
#include "AsceEtherCores.h"

#include "AsceMass/Ether/AsceEtherNetworkSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AsceEtherCores)

namespace
{
	/** 把 int32 参数夹到 int16（编号）有效范围。 */
	FORCEINLINE int16 ToSignalType(int32 Value)
	{
		return static_cast<int16>(FMath::Clamp<int32>(Value, -32767, 32767));
	}
}

// ─────────────────────────────────────────────────────────────
// 传输器
// ─────────────────────────────────────────────────────────────

UAsceEtherTransporterCore::UAsceEtherTransporterCore()
{
	bInstance = false; // 无状态，共用 CDO
}

void UAsceEtherTransporterCore::OnInit(UAsceEtherNetworkSubsystem& Net, int32 NodeId)
{
	// 传输器不区分入出：全部交给"内部布线结构"定向（有 LinkTargets = 收，否则 = 发）
	const int32 PortNum = Net.GetNodePortCount(NodeId);
	for (int32 i = 0; i < PortNum; ++i)
	{
		if (FAsceEtherPortData* P = Net.GetPort(NodeId, i))
		{
			P->TransportType = EAsceEtherTransportType::Any;
		}
	}
}

// ─────────────────────────────────────────────────────────────
// 设施 / 汇
// ─────────────────────────────────────────────────────────────

void UAsceEtherFacilityCore::OnInit(UAsceEtherNetworkSubsystem& Net, int32 NodeId)
{
	// 端口约定：第一个端口 = 输出，其余 = 输入
	const int32 PortNum = Net.GetNodePortCount(NodeId);
	for (int32 i = 0; i < PortNum; ++i)
	{
		if (FAsceEtherPortData* P = Net.GetPort(NodeId, i))
		{
			P->TransportType = (i == 0) ? EAsceEtherTransportType::Output : EAsceEtherTransportType::Input;
		}
	}
}

void UAsceEtherFacilityCore::Tick(UAsceEtherNetworkSubsystem& Net, int32 NodeId)
{
	const int32 PortNum = Net.GetNodePortCount(NodeId);
	if (PortNum <= 0 || RecipeOutput.AmountPerTick <= 0)
	{
		return;
	}

	const int32 OutIdx = Net.FindPortByTransport(NodeId, EAsceEtherTransportType::Output);
	if (OutIdx < 0)
	{
		return;
	}
	FAsceEtherPortData* Out = Net.GetPort(NodeId, OutIdx);
	if (!Out)
	{
		return;
	}

	const int16 OutType = ToSignalType(RecipeOutput.SignalType);
	const uint16 OutRem = (Out->Throughput > Out->Ether) ? static_cast<uint16>(Out->Throughput - Out->Ether) : 0;
	const bool bOutTypeOk = (Out->Ether == 0) || (Out->SignalType == OutType);

	// 配方够不够（空配方 = 免费源）
	bool bEnough = true;
	for (const FAsceEtherRecipeItem& Item : RecipeInputs)
	{
		const int16 Want = ToSignalType(Item.SignalType);
		uint32 Avail = 0;
		for (int32 i = 0; i < PortNum; ++i)
		{
			const FAsceEtherPortData* P = Net.GetPort(NodeId, i);
			if (P && P->TransportType == EAsceEtherTransportType::Input && P->Ether > 0 && P->SignalType == Want)
			{
				Avail += P->Ether;
			}
		}
		if (Avail < static_cast<uint32>(FMath::Max(0, Item.AmountPerTick)))
		{
			bEnough = false;
			break;
		}
	}

	if (!bEnough || !bOutTypeOk || OutRem < RecipeOutput.AmountPerTick)
	{
		return; // 料不够 / 出口类型不对 / 出口容量不足 ⇒ 本拍不产
	}

	// 扣料
	for (const FAsceEtherRecipeItem& Item : RecipeInputs)
	{
		const int16 Want = ToSignalType(Item.SignalType);
		uint32 Need = static_cast<uint32>(FMath::Max(0, Item.AmountPerTick));
		for (int32 i = 0; i < PortNum && Need > 0; ++i)
		{
			FAsceEtherPortData* P = Net.GetPort(NodeId, i);
			if (P && P->TransportType == EAsceEtherTransportType::Input && P->Ether > 0 && P->SignalType == Want)
			{
				const uint16 Take = static_cast<uint16>(FMath::Min<uint32>(P->Ether, Need));
				P->Ether = static_cast<uint16>(P->Ether - Take);
				Need -= Take;
				if (P->Ether == 0)
				{
					P->SignalType = 0;
				}
			}
		}
	}

	if (Out->Ether == 0)
	{
		Out->SignalType = OutType;
	}
	Out->Ether = static_cast<uint16>(Out->Ether + RecipeOutput.AmountPerTick);
}

void UAsceEtherSinkCore::OnInit(UAsceEtherNetworkSubsystem& Net, int32 NodeId)
{
	const int32 PortNum = Net.GetNodePortCount(NodeId);
	for (int32 i = 0; i < PortNum; ++i)
	{
		if (FAsceEtherPortData* P = Net.GetPort(NodeId, i))
		{
			P->TransportType = EAsceEtherTransportType::Input;
		}
	}
}

void UAsceEtherSinkCore::Tick(UAsceEtherNetworkSubsystem& Net, int32 NodeId)
{
	const int32 PortNum = Net.GetNodePortCount(NodeId);
	for (int32 i = 0; i < PortNum; ++i)
	{
		Net.ClearPort(NodeId, i);
	}
}

// ─────────────────────────────────────────────────────────────
// 记忆单元 / 时钟 / 计数器
// ─────────────────────────────────────────────────────────────

UAsceEtherLatchCore::UAsceEtherLatchCore()
{
	bInstance = true; // 有状态
}

void UAsceEtherLatchCore::OnInit(UAsceEtherNetworkSubsystem& Net, int32 NodeId)
{
	const int32 PortNum = Net.GetNodePortCount(NodeId);
	for (int32 i = 0; i < PortNum; ++i)
	{
		if (FAsceEtherPortData* P = Net.GetPort(NodeId, i))
		{
			P->TransportType = (i == 0) ? EAsceEtherTransportType::Output : EAsceEtherTransportType::Input;
		}
	}
}

void UAsceEtherLatchCore::Tick(UAsceEtherNetworkSubsystem& Net, int32 NodeId)
{
	const int32 PortNum = Net.GetNodePortCount(NodeId);

	// 写入：入端口出现"非 0 号"的料 ⇒ 覆盖编号（消耗掉那份料；旧值直接覆盖 = 信息不守恒）
	for (int32 i = 0; i < PortNum; ++i)
	{
		FAsceEtherPortData* P = Net.GetPort(NodeId, i);
		if (P && P->TransportType == EAsceEtherTransportType::Input && P->Ether > 0 && AsceEther::Family(P->SignalType) != 0)
		{
			StoredType = P->SignalType;
			bHasStored = true;
			Net.ClearPort(NodeId, i);
		}
	}

	// 读取：用 0 号 ether（能量）重铸编号
	if (bHasStored)
	{
		const int32 OutIdx = Net.FindPortByTransport(NodeId, EAsceEtherTransportType::Output);
		Net.MintFromEnergy(NodeId, OutIdx, ToSignalType(StoredType));
	}
}

FString UAsceEtherLatchCore::GetDebugString() const
{
	return FString::Printf(TEXT("存储:%s%d"), StoredType < 0 ? TEXT("-") : TEXT("+"), FMath::Abs(StoredType));
}

UAsceEtherClockCore::UAsceEtherClockCore()
{
	bInstance = true;
}

void UAsceEtherClockCore::OnInit(UAsceEtherNetworkSubsystem& Net, int32 NodeId)
{
	const int32 PortNum = Net.GetNodePortCount(NodeId);
	for (int32 i = 0; i < PortNum; ++i)
	{
		if (FAsceEtherPortData* P = Net.GetPort(NodeId, i))
		{
			P->TransportType = (i == 0) ? EAsceEtherTransportType::Output : EAsceEtherTransportType::Input;
		}
	}
}

void UAsceEtherClockCore::Tick(UAsceEtherNetworkSubsystem& Net, int32 NodeId)
{
	if (CurrentType == 0)
	{
		// 0 号 = 能量，不能当时钟值（铸币需要能量，Cost 不能为 0）⇒ 参数填 0 时兜底用 1
		CurrentType = (OutputSignalType != 0) ? OutputSignalType : 1;
		Phase = 0;
	}

	const int32 Half = FMath::Max(1, HalfPeriodTicks);
	if (++Phase >= Half)
	{
		CurrentType = AsceEther::Complement(ToSignalType(CurrentType));
		Phase = 0;
	}

	const int32 OutIdx = Net.FindPortByTransport(NodeId, EAsceEtherTransportType::Output);
	Net.MintFromEnergy(NodeId, OutIdx, ToSignalType(CurrentType));
}

FString UAsceEtherClockCore::GetDebugString() const
{
	return FString::Printf(TEXT("时钟:%s%d 相位:%d/%d"),
		CurrentType < 0 ? TEXT("-") : TEXT("+"), FMath::Abs(CurrentType), Phase, FMath::Max(1, HalfPeriodTicks));
}

UAsceEtherCounterCore::UAsceEtherCounterCore()
{
	bInstance = true;
}

void UAsceEtherCounterCore::OnInit(UAsceEtherNetworkSubsystem& Net, int32 NodeId)
{
	const int32 PortNum = Net.GetNodePortCount(NodeId);
	for (int32 i = 0; i < PortNum; ++i)
	{
		if (FAsceEtherPortData* P = Net.GetPort(NodeId, i))
		{
			P->TransportType = (i == 0) ? EAsceEtherTransportType::Output : EAsceEtherTransportType::Input;
		}
	}
}

void UAsceEtherCounterCore::Tick(UAsceEtherNetworkSubsystem& Net, int32 NodeId)
{
	const int32 PortNum = Net.GetNodePortCount(NodeId);

	// 计数：入端口每来 1 份"非 0 号"料 = 1 个脉冲（0 号 = 能量，不计数）
	for (int32 i = 0; i < PortNum; ++i)
	{
		FAsceEtherPortData* P = Net.GetPort(NodeId, i);
		if (P && P->TransportType == EAsceEtherTransportType::Input && P->Ether > 0 && AsceEther::Family(P->SignalType) != 0)
		{
			Progress = FMath::Min<int32>(INT32_MAX - P->Ether, Progress + P->Ether);
			Net.ClearPort(NodeId, i);
		}
	}

	// 满阈值 ⇒ 产 1 份编号（用能量重铸）
	const int32 Need = FMath::Max(1, Threshold);
	if (Progress >= Need)
	{
		const int32 OutIdx = Net.FindPortByTransport(NodeId, EAsceEtherTransportType::Output);
		if (Net.MintFromEnergy(NodeId, OutIdx, ToSignalType(OutputSignalType)))
		{
			Progress -= Need;
		}
	}
}

FString UAsceEtherCounterCore::GetDebugString() const
{
	return FString::Printf(TEXT("计数:%d/%d"), Progress, FMath::Max(1, Threshold));
}

// ─────────────────────────────────────────────────────────────
// 寄存器
// ─────────────────────────────────────────────────────────────

UAsceEtherRegisterCore::UAsceEtherRegisterCore()
{
	bInstance = true;
	for (int32 i = 0; i < 8; ++i)
	{
		StoredBits[i] = 0;
	}
}

void UAsceEtherRegisterCore::OnInit(UAsceEtherNetworkSubsystem& Net, int32 NodeId)
{
	// 端口约定：[bit0..bitN-1 (Input), bit0..bitN-1 (Output), enable (Any)]
	const int32 Bits = FMath::Clamp(BitCount, 1, 8);
	for (int32 i = 0; i < Bits; ++i)
	{
		if (FAsceEtherPortData* P = Net.GetPort(NodeId, i))
		{
			P->TransportType = EAsceEtherTransportType::Input;
		}
		if (FAsceEtherPortData* Q = Net.GetPort(NodeId, Bits + i))
		{
			Q->TransportType = EAsceEtherTransportType::Output;
		}
	}
	if (FAsceEtherPortData* E = Net.GetPort(NodeId, Bits * 2))
	{
		E->TransportType = EAsceEtherTransportType::Any; // enable
	}
}

void UAsceEtherRegisterCore::Tick(UAsceEtherNetworkSubsystem& Net, int32 NodeId)
{
	const int32 Bits = FMath::Clamp(BitCount, 1, 8);

	// 使能 = enable 端口收到"正 ether"（非 0 号、非互补态）
	bool bWrite = false;
	const int32 EnableIdx = Net.FindPortByTransport(NodeId, EAsceEtherTransportType::Any);
	if (EnableIdx >= 0)
	{
		const FAsceEtherPortData* EP = Net.GetPort(NodeId, EnableIdx);
		bWrite = EP && EP->Ether > 0 && AsceEther::Family(EP->SignalType) != 0 && !AsceEther::IsComplement(EP->SignalType);
	}

	if (bWrite)
	{
		// 并行加载：有料 ⇒ 覆盖该位；没料 ⇒ 清空该位
		for (int32 i = 0; i < Bits; ++i)
		{
			FAsceEtherPortData* P = Net.GetPort(NodeId, i);
			if (P && P->Ether > 0 && AsceEther::Family(P->SignalType) != 0)
			{
				StoredBits[i] = P->SignalType;
			}
			else
			{
				StoredBits[i] = 0;
			}
			Net.ClearPort(NodeId, i);
		}
		Net.ClearPort(NodeId, EnableIdx);
		bHasStored = true;
	}

	// 读取：把有值的位用 0 号能量重铸到对应输出口
	if (bHasStored)
	{
		for (int32 i = 0; i < Bits; ++i)
		{
			if (StoredBits[i] != 0)
			{
				Net.MintFromEnergy(NodeId, Bits + i, ToSignalType(StoredBits[i]));
			}
		}
	}
}

FString UAsceEtherRegisterCore::GetDebugString() const
{
	FString Bits;
	for (int32 i = 0; i < 8; ++i)
	{
		Bits += (i < BitCount && StoredBits[i] != 0) ? TEXT("1") : TEXT("0");
	}
	return FString::Printf(TEXT("寄存器:%s"), *Bits);
}

// ─────────────────────────────────────────────────────────────
// 湮灭发电机
// ─────────────────────────────────────────────────────────────

UAsceEtherAnnihilatorCore::UAsceEtherAnnihilatorCore()
{
	bInstance = false;
}

void UAsceEtherAnnihilatorCore::OnInit(UAsceEtherNetworkSubsystem& Net, int32 NodeId)
{
	// 端口约定：第一个端口 = 输出，其余 = 输入
	const int32 PortNum = Net.GetNodePortCount(NodeId);
	for (int32 i = 0; i < PortNum; ++i)
	{
		if (FAsceEtherPortData* P = Net.GetPort(NodeId, i))
		{
			P->TransportType = (i == 0) ? EAsceEtherTransportType::Output : EAsceEtherTransportType::Input;
		}
	}
}

void UAsceEtherAnnihilatorCore::Tick(UAsceEtherNetworkSubsystem& Net, int32 NodeId)
{
	const int32 PortNum = Net.GetNodePortCount(NodeId);

	// 找一组同族异号（+X 与 −X）
	int32 PlusIdx = INDEX_NONE;
	int32 MinusIdx = INDEX_NONE;
	for (int32 i = 0; i < PortNum; ++i)
	{
		const FAsceEtherPortData* P = Net.GetPort(NodeId, i);
		if (!P || P->TransportType != EAsceEtherTransportType::Input || P->Ether == 0 || AsceEther::Family(P->SignalType) == 0)
		{
			continue;
		}
		if (AsceEther::IsComplement(P->SignalType))
		{
			if (MinusIdx == INDEX_NONE)
			{
				MinusIdx = i;
			}
		}
		else if (PlusIdx == INDEX_NONE)
		{
			PlusIdx = i;
		}
	}
	if (PlusIdx == INDEX_NONE || MinusIdx == INDEX_NONE)
	{
		return;
	}

	FAsceEtherPortData* SP = Net.GetPort(NodeId, PlusIdx);
	FAsceEtherPortData* SM = Net.GetPort(NodeId, MinusIdx);
	if (!SP || !SM || !AsceEther::IsSameFamily(SP->SignalType, SM->SignalType))
	{
		return;
	}

	const int32 OutIdx = Net.FindPortByTransport(NodeId, EAsceEtherTransportType::Output);
	FAsceEtherPortData* Out = Net.GetPort(NodeId, OutIdx);
	if (!Out)
	{
		return;
	}

	const uint16 FamilyId = AsceEther::Family(SP->SignalType); // = |X|（核数）
	if (FamilyId == 0)
	{
		return;
	}
	const uint16 OutRem = (Out->Throughput > Out->Ether) ? static_cast<uint16>(Out->Throughput - Out->Ether) : 0;
	const bool bOutTypeOk = (Out->Ether == 0) || (Out->SignalType == 0);
	uint32 Pairs = FMath::Min<uint16>(SP->Ether, SM->Ether);
	Pairs = FMath::Min<uint32>(Pairs, OutRem / FamilyId);
	if (!bOutTypeOk || Pairs == 0)
	{
		return;
	}

	SP->Ether = static_cast<uint16>(SP->Ether - Pairs);
	SM->Ether = static_cast<uint16>(SM->Ether - Pairs);
	if (SP->Ether == 0)
	{
		SP->SignalType = 0;
	}
	if (SM->Ether == 0)
	{
		SM->SignalType = 0;
	}
	if (Out->Ether == 0)
	{
		Out->SignalType = 0; // 0 号 = 能量
	}
	Out->Ether = static_cast<uint16>(Out->Ether + Pairs * FamilyId);
}
