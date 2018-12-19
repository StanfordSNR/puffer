#!/usr/bin/env python3

import torch


def main():
    N = 32
    D_in, H, D_out = 5, 4, 3

    # TODO: read data from InfluxDB into x and y
    x = torch.randn(N, D_in)
    y = torch.randn(N, D_out)

    model = torch.nn.Sequential(
        torch.nn.Linear(D_in, H),
        torch.nn.ReLU(),
        torch.nn.Linear(H, D_out),
    )
    loss_fn = torch.nn.MSELoss()

    learning_rate = 1e-4
    optimizer = torch.optim.Adam(model.parameters(), lr=learning_rate)

    max_iters = 500
    for t in range(max_iters):
        y_pred = model(x)

        loss = loss_fn(y_pred, y)
        print(t, loss.item())

        optimizer.zero_grad()
        loss.backward()
        optimizer.step()


if __name__ == '__main__':
    main()
