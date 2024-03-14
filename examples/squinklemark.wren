import "basil" for Color, Image, OS, Timer, Window
import "random" for Random

var random = Random.new()

class Squinkle {
    construct new(x, y, vx, vy, color) {
        _x = x
        _y = y
        _vx = vx
        _vy = vy
        _color = color
    }

    x { _x }
    y { _y }
    vx { _vx }
    vy { _vy }
    color { _color }

    x=(v) { _x = v }
    y=(v) { _y = v }
    vx=(v) { _vx = v }
    vy=(v) { _vy = v }
}

var screenWidth = 320
var screenHeight = 240

var screen = Image.new(screenWidth, screenHeight)
Window.init("Squinklemark", screenWidth * 2, screenHeight * 2)

var timer = Timer.new()

var squinkleImage = Image.new("../assets/squinkle.png")

var squinkles = []

System.print("Basil version " + OS.basilVersion + " <3")

while (!Window.closed) {
    if (Window.mouseHeld(1)) {
        for (i in 1..100) {
            var squinkle = Squinkle.new(
                Window.mouseX,
                Window.mouseY,
                random.float(-250, 250) / 60,
                random.float(-250, 250) / 60,
                Color.new(
                    random.int(50, 240),
                    random.int(80, 240),
                    random.int(100, 240)
                )
            )

            squinkles.add(squinkle)
        }
    }

    for (s in squinkles) {
        s.x = s.x + s.vx
        s.y = s.y + s.vy

        if (((s.x + squinkleImage.width / 2) > screenWidth) || (s.x + squinkleImage.width / 2 < 0)) {
            s.vx = -s.vx
        }

        if (((s.y + squinkleImage.height / 2) > screenHeight) || (s.y + squinkleImage.height / 2 < 0)) {
            s.vy = -s.vy
        }
    }

    screen.clear(Color.white)

    for (s in squinkles) {
        screen.blitAlpha(squinkleImage, s.x, s.y, 0, 0, squinkleImage.width, squinkleImage.height, s.color)
    }

    screen.fill(0, 0, screenWidth, 28, Color.black)

    screen.text("FPS: %((1 / timer.delta).ceil) Squinkles: %(squinkles.count)", 10, 10, Color.white)

    Window.update(screen)
    timer.tick(60)
}

Window.quit()
