# HookCrashers Enums

These constants are exposed to Lua through `HC.Enums` and the global `Enums`
table. Use them when registering characters:

```lua
HC.Character.Register(
    "my_character",
    HC.Enums.Weapon.Pitchfork,
    HC.Enums.Pet.None,
    true,
    false
)
```

## Weapons

| Name | Hex | Decimal |
| --- | ---: | ---: |
| `SkinnySword` | `0x02` | 2 |
| `ThinSword` | `0x03` | 3 |
| `ThickSword` | `0x04` | 4 |
| `PumpkinPeeler` | `0x05` | 5 |
| `GladiatorSword` | `0x06` | 6 |
| `ButcherKnife` | `0x07` | 7 |
| `HalfSword` | `0x08` | 8 |
| `Carrot` | `0x09` | 9 |
| `ThiefSword` | `0x0A` | 10 |
| `GoldSword` | `0x0B` | 11 |
| `DualProngSword` | `0x0C` | 12 |
| `Zigzag` | `0x0D` | 13 |
| `PlaydoPastaMaker` | `0x0E` | 14 |
| `Falchion` | `0x0F` | 15 |
| `PointySword` | `0x10` | 16 |
| `ChewedUpSword` | `0x11` | 17 |
| `FencersFoil` | `0x12` | 18 |
| `BarbarianAxe` | `0x13` | 19 |
| `Pitchfork` | `0x14` | 20 |
| `CurvedSword` | `0x15` | 21 |
| `KeySword` | `0x16` | 22 |
| `ApplePeeler` | `0x17` | 23 |
| `RubberHandleSword` | `0x18` | 24 |
| `Mace` | `0x19` | 25 |
| `Club` | `0x1A` | 26 |
| `UglyMace` | `0x1B` | 27 |
| `RefinedMace` | `0x1C` | 28 |
| `Fish` | `0x1D` | 29 |
| `WrappedSword` | `0x1E` | 30 |
| `SkeletorMace` | `0x1F` | 31 |
| `ClunkyMace` | `0x20` | 32 |
| `SnakeyMace` | `0x21` | 33 |
| `RatBeatingBat` | `0x22` | 34 |
| `BlackMorningStar` | `0x23` | 35 |
| `KingsMace` | `0x24` | 36 |
| `MeatTenderizer` | `0x25` | 37 |
| `Leaf` | `0x26` | 38 |
| `SheathedSword` | `0x27` | 39 |
| `PracticeFoil` | `0x28` | 40 |
| `Twig` | `0x29` | 41 |
| `LeafyTwig` | `0x2A` | 42 |
| `LightSaber` | `0x2B` | 43 |
| `Staff` | `0x2C` | 44 |
| `WoodenSpoon` | `0x2D` | 45 |
| `BoneLeg` | `0x2E` | 46 |
| `AlienGun` | `0x2F` | 47 |
| `FishingSpear` | `0x30` | 48 |
| `Lance` | `0x31` | 49 |
| `Sai` | `0x32` | 50 |
| `UnicornHorn` | `0x33` | 51 |
| `Ribeye` | `0x34` | 52 |
| `Kielbasa` | `0x35` | 53 |
| `Lobster` | `0x36` | 54 |
| `Umbrella` | `0x37` | 55 |
| `BroadAx` | `0x38` | 56 |
| `EvilSword` | `0x39` | 57 |
| `IceSword` | `0x3A` | 58 |
| `Candlestick` | `0x3B` | 59 |
| `PanicMallet` | `0x3C` | 60 |
| `FishingRod` | `0x3D` | 61 |
| `Wrench` | `0x3E` | 62 |
| `NGLollipop` | `0x3F` | 63 |
| `GoldSkullMace` | `0x40` | 64 |
| `NGGoldSword` | `0x41` | 65 |
| `Chainsaw` | `0x42` | 66 |
| `BroadSpear` | `0x43` | 67 |
| `Glowstick` | `0x44` | 68 |
| `ChickenStick` | `0x45` | 69 |
| `DemonSword` | `0x46` | 70 |
| `BroccoliSword` | `0x47` | 71 |
| `ManCatcher` | `0x48` | 72 |
| `WoodenMace` | `0x49` | 73 |
| `NinjaClaw` | `0x4A` | 74 |
| `BuffaloMace` | `0x4B` | 75 |
| `ElectricEel` | `0x4C` | 76 |
| `Scissors` | `0x4D` | 77 |
| `DinnerFork` | `0x4E` | 78 |
| `CattleProd` | `0x4F` | 79 |
| `LightningBolt` | `0x50` | 80 |
| `TwoByFour` | `0x51` | 81 |
| `WoodenSword` | `0x52` | 82 |
| `CardboardTube` | `0x53` | 83 |
| `EmeraldSword` | `0x54` | 84 |
| `Hammer` | `0x55` | 85 |
| `Pencil` | `0x56` | 86 |

## Pets

| Name | Hex | Decimal |
| --- | ---: | ---: |
| `None` | `0x00` | 0 |
| `Cardinal` | `0x01` | 1 |
| `Owlet` | `0x02` | 2 |
| `Rammy` | `0x03` | 3 |
| `Frogglet` | `0x04` | 4 |
| `Monkeyface` | `0x05` | 5 |
| `BiPolarBear` | `0x06` | 6 |
| `BiteyBat` | `0x07` | 7 |
| `Yeti` | `0x08` | 8 |
| `Troll` | `0x09` | 9 |
| `Snailburt` | `0x0A` | 10 |
| `Giraffey` | `0x0B` | 11 |
| `Zebra` | `0x0C` | 12 |
| `Meowburt` | `0x0D` | 13 |
| `Pazzo` | `0x0E` | 14 |
| `BurlyBear` | `0x0F` | 15 |
| `Hawkster` | `0x10` | 16 |
| `Snoot` | `0x11` | 17 |
| `Piggy` | `0x12` | 18 |
| `Spiny` | `0x13` | 19 |
| `Scratchpaw` | `0x14` | 20 |
| `Seahorse` | `0x15` | 21 |
| `Chicken` | `0x16` | 22 |
| `InstallBall` | `0x17` | 23 |
| `MrBuddy` | `0x18` | 24 |
| `Sherbert` | `0x19` | 25 |
| `Pelter` | `0x1A` | 26 |
| `Dragonhead` | `0x1B` | 27 |
| `Beholder` | `0x1C` | 28 |
| `GoldenWhale` | `0x1D` | 29 |
