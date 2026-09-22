using FFragmentFieldValueChangedDelegate = TDelegate<void(UObject*, FFragmentFieldId), FNotThreadSafeNotCheckedDelegateUserPolicy>;

struct FFragmentFieldId:
   FName FragmentFieldName
   int32 BitNumber
   //排序只比较FragmentFieldName
   operator==(FFragmentFieldId& Other):
      return Other.FragmentFieldName == FragmentFieldName

struct FCacheField:
   FProperty* Property
   void* Buffer

struct BindElement:
   FFragmentFieldId FieldId
   FFragmentFieldValueChangedDelegate Delegate

struct FEntityFragmentSampler:
   FMassEntityHandle EntityHandle
   UStruct* FragmentType
   TBitArray BitNumberArray
   TFieldPointerArray<FCacheField> FieldArray
   TArray<BindElement> BindElements
   bool BindElementsLock
   int32 BindDelegatesEnd
   int32 CompactionCount

   Init():
      const int propertynum = 0
      for (TFieldIterator<FProperty> It(FragmentType); It; ++It):
         FCacheField CacheField
         CacheField.Property = *It;
         const int32 Size = *It->GetSize()
         const int32 Align = *It->GetMinAlignment()
         CacheField.Buffer = FMemory::Malloc(Size, Align)
         FieldArray.Emplace(CacheField)
         propertynum++
      BitNumberArray.Resize(proeprtynum)
      return   

   Capture(FFragmentFieldId FieldId):
      FCachedField& CachedField = FieldArray[FieldId.BitNumber]
      FEntity& Entity = GetEntity(EntityHandle)
      FStructView View = FMassEntityView(EntityManager, Entity).GetFragmentDataStruct(FragmentType)
      if(!View.IsValid()):
         return
      void* FragmentPtr = View.GetMemory()

      // 不调CachedField.Property->InitializeValue(CachedField.Buffer)，就是纯副本，不触发构造和析构CachedField.Property->DestroyValue(CachedField.Buffer)
      
      void* FieldPtr = Prop->ContainerPtrToValuePtr<void>(FragmentPtr)
      const int32 Size = CacheField.Property->GetSize()
      FMemory::Memcmp(FieldPtr, CacheField.Buffer, Size) == 0:
         //一致，啥也不干
         return
      
      FMemory::Memcpy(FieldPtr, CacheField.Buffer, Size)
      Broadcast(FieldId)
      return

   Release():
      for (int i = 0; i < FieldArray.Num(), ++i):
         // 不调CachedField.Property->DestroyValue(CachedField.Buffer)，就是纯副本，不触发构造和析构
         FMemory::Free(CacheField.Buffer)
         CachedField.Property = nullptr
         CachedField.Buffer = nullptr
      return

   AddDelegate(FFragmentFieldId FieldId, FFragmentFieldValueChangedDelegate Delegate):
      if BindElementsLock:
         BindingDelegates.Emplace(FBindDelegate(FieldId, Delegate))
      else:
         //二分插入
      BitNumberArray[FieldId.BitNumber] = true:
      return

   RemoveDelegate(FFragmentFieldId FieldId, FFragmentFieldValueChangedDelegate Delegate):
      二分查找到FFragmentFieldValueChangedDelegate BindElement
      if BindElementsLock:
         BindElement.Delegate.UnBind()
         ++CompactionCount
      else:
         直接删除

      从BindElement的位置开始向前和向后看一下，如果前面或者后面后面还有相同的FieldId的delegate，直接return
      否则说明这个类型的FieldId没有绑定了:
         BitNumberArray[FieldId.BitNumber] = False
      return

   Broadcast(FFragmentFieldId FieldId):
      BindElementsLock = True
      BindElementEnd = BindElements.Num() - 1

      二分查找FieldId所在的开头
      遍历executeifbound直到不是FieldId对应的委托
      
      if CompactionCount > 0:
         for Element in BindElements:
            if Element.Delegate.IsCompactable():
               BindElements.RemoveAt(Index)
               --BindElementEnd
         CompactionCount = 0
      for(; BindElementEnd < BindElements.Num(); ):
         if (Delegates[AddedEmplaceAt].Delegate.IsBound()):
            const int32 FoundIndex = UpperBound(Delegates[AddedEmplaceAt].Key);
            if (FoundIndex != AddedEmplaceAt)：          // 需要挪才挪
                FInvocationElement Element = MoveTemp(Delegates[AddedEmplaceAt]);
                Delegates.RemoveAtSwap(AddedEmplaceAt); // 从尾巴摘掉
                Delegates.EmplaceAt(FoundIndex, MoveTemp(Element)); // 插到有序位置
            ++AddedEmplaceAt;                           // 已归位，边界前移
        else：
            Delegates.RemoveAtSwap(AddedEmplaceAt);     // 已解绑，直接丢
      BindElementsLock = False
      return