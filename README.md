# KDEck Control

KDEck Control is a KDE Plasma app for the Stream Deck on the Corsair Galleon 100 SD. It drives the 12 LCD keys, the top screen, and the two dials.

This project is based on [Galleon Deck](https://github.com/NLMP-DDHS/galleon-deck) by [NLMP-DDHS](https://github.com/NLMP-DDHS).

## Install

```sh
git clone https://github.com/RyanHakurei/KDEck-Control
cd KDEck-Control/packaging/arch-qt
makepkg -si
systemctl --user enable --now kdeck-control
```

Open **KDEck Control** from the app menu, or run `kdeck-control-config`.

Profiles are stored in `~/.config/kdeck-control`. If that directory has no `config.toml`, an existing `~/.config/galleon-deck` is read in place.

## License

MIT, see [LICENSE](LICENSE).
